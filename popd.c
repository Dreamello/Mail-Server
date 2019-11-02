#include "server.h"
#include "socketbuffer.h"
#include "user.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#define MAX_LINE_LENGTH 1024

static void handle_client(int fd);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
        return 1;
    }

    run_server(argv[1], handle_client);

    return 0;
}

/** Sends a welcome message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int sendWelcome(int fd) {
    return send_string(fd, "+OK POP3 Server Ready\r\n");
}

/** Sends a positive message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int sendPositive(int fd) {
    return send_string(fd, "+OK\r\n");
}

/** Sends a negative message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int sendNegative(int fd) {
    return send_string(fd, "-ERR\r\n");
}

/** Sends a message with +OK and mailCount and mailListSize to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int sendCountPositive(int fd, unsigned int mailCount, size_t size) {
    char count[MAX_LINE_LENGTH + 1] = "";
    sprintf(count, "+OK %d %d\r\n", mailCount, (int)size);
    return send_all(fd, count, strlen(count));
}

/** Sends a message with mailCount and mailListSize to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int sendCount(int fd, unsigned int mailCount, size_t size) {
    char count[MAX_LINE_LENGTH + 1] = "";
    sprintf(count, "%d %d\r\n", mailCount, (int)size);
    return send_all(fd, count, strlen(count));
}

/** Returns the arguments of given string. String is modified.
 *
 *  Parameters: str: String containing arguments
 *
 *  Return: the starting char of arguments, or NULL if argument syntax is incorrect.
 */
char* retrieveArgs(char* str) {
    // returns the next char after "<"
    char* ret = strchr(str, ' ');
    if (ret)
        return strtok(++ret, "\r");
    return ret;
}

/** Check if given string only contains numbers.
 *
 *  Parameters: str: String to be check
 *
 *  Return: 1 if number, 0 otherwise
 */
int numbers_only(char* s) {
    while (*s)
        if (isdigit(*s++) == 0) return 0;
    return 1;
}

/** Checks if given string ends with CRLF, or contains any trailing spaces, or only contains CRLF
 *
 *  Parameters: str: Socket file descriptor.
 *              size: Size of string
 *
 *  Return: 1 if CRLF is at the end, with at least another char in the string,
 *             and string has no trailing spaces, 0 otherwise
 */
int checkCRLF(char* str, int size) {
    // contains only CRLF
    if (size == 2 && str[size - 2] == 0xd && str[size - 1] == 0xa)
        return 0;

    // if ending with CRLF, and the character before CRLF is not a space
    if (size > 3 && str[size - 2] == 0xd && str[size - 1] == 0xa) {
        if (!isspace(str[size - 3]))
            return 1;
    }

    return 0;
}

/** Checks if given string ends with CRLF.
 *
 *  Parameters: str: String to be checked
 *              size: Size of string
 *
 *  Return: 1 if CRLF is at the end, 0 otherwise
 */
int checkCRLFSimple(char* str, int size) {
    if (size > 1 && str[size - 2] == 0xd && str[size - 1] == 0xa)
        return 1;
    return 0;
}

/** Read given email.
 *
 *  Parameters: fd: String to be checked
 *              mail: pointer to mail item that needs to be read
 *
 *  Return: 1 if CRLF is at the end, 0 otherwise
 */
int readEmail(int fd, mail_item_t mail) {
    int send_status;
    int readfd = open(get_mail_item_filename(mail), O_RDONLY);

    if (readfd > 0) {
        // no open error
        char* content = malloc((MAX_LINE_LENGTH * sizeof(char)) + 1);
        send_status = sendPositive(fd);
        int i = 0;

        // read the file one character at a time
        while (read(readfd, &content[i], 1) == 1) {
            // we keep reading until we see a '\n'
            if (content[i] == '\n') {
                // at end of line, we should now send this line
                // we add a null terminator
                content[i + 1] = '\0';
                send_all(fd, content, strlen(content));
                i = 0;
                continue;
            }
            i++;  // we did not reach the end of the line, so we keep going!
        }

        // entire message has been read
        send_status = send_string(fd, ".\r\n");
        close(readfd);
        free(content);

    } else {
        send_status = sendNegative(fd);
    }
    return send_status;
}

void handle_client(int fd) {
    // State variables
    int authorization = 1;
    int transaction = 0;
    int accepted_user = 0;

    // other variables
    int reply_size;
    int send_status = 1;
    char command[41];
    char reply[MAX_LINE_LENGTH + 1];
    char username[MAX_USERNAME_SIZE + 1];
    char password[MAX_USERNAME_SIZE + 1];
    socket_buffer_t buffer = sb_create(fd, MAX_LINE_LENGTH);

    mail_item_t mail;
    mail_list_t mailList;
    unsigned int mailCount = 0;

    // initial message
    send_status = sendWelcome(fd);

    while (1) {
        reply_size = sb_read_line(buffer, reply);

        if (reply_size == 0 || reply_size == -1 || send_status == -1) break;

        sscanf(reply, "%s", command);

        // check line ends with CRLF, and does not contain any trailing whitespaces. This can
        //       also detect if line is too long
        if (checkCRLF(reply, reply_size)) {
            // STATE: AUTHORIZATION
            if (authorization) {
                if (strcasecmp(command, "USER") == 0) {
                    // if size == 6, no arguments was received
                    if (reply_size != 6) {
                        // stores argument into username string
                        strcpy(username, retrieveArgs(reply));

                        if (is_valid_user(username, NULL)) {
                            accepted_user = 1;
                            send_status = sendPositive(fd);

                        } else {
                            send_status = sendNegative(fd);
                        }

                    } else {
                        send_status = sendNegative(fd);
                    }

                } else if (strcasecmp(command, "PASS") == 0) {
                    // if size == 6, no arguments was received
                    if (accepted_user && reply_size != 6) {
                        // stores arguments into password string
                        strcpy(password, retrieveArgs(reply));

                        if (is_valid_user(username, password)) {
                            // valid user
                            authorization = 0;
                            transaction = 1;
                            mailList = load_user_mail(username);
                            mailCount = get_mail_count(mailList);
                            send_status = sendPositive(fd);

                        } else {
                            // invalid user
                            accepted_user = 0;
                            send_status = sendNegative(fd);
                        }

                    } else {
                        accepted_user = 0;
                        send_status = sendNegative(fd);
                    }

                } else if (strcasecmp(command, "QUIT") == 0 && reply_size == 6) {
                    send_status = sendPositive(fd);
                    break;

                } else {
                    send_status = sendNegative(fd);
                }
            }

            // STATE: TRANSACTION
            else if (transaction) {
                if (strcasecmp(command, "STAT") == 0 && reply_size == 6) {
                    send_status = sendCountPositive(fd, get_mail_count(mailList), get_mail_list_size(mailList));

                } else if (strcasecmp(command, "LIST") == 0) {
                    if (reply_size == 6) {
                        // no arguments
                        send_status = sendCountPositive(fd, get_mail_count(mailList), get_mail_list_size(mailList));

                        for (unsigned int i = 0; i < mailCount; i++) {
                            if ((mail = get_mail_item(mailList, i)))
                                send_status = sendCount(fd, i + 1, get_mail_item_size(mail));
                        }
                        send_status = send_string(fd, ".\r\n");

                    } else {
                        // contains arguments
                        char* arg = retrieveArgs(reply);

                        // check if arguments only contain numbers
                        if (arg && numbers_only(arg)) {
                            unsigned int index = (int)strtol(arg, NULL, 10);

                            if ((mail = get_mail_item(mailList, index - 1)))
                                send_status = sendCountPositive(fd, index, get_mail_item_size(mail));
                            else
                                send_status = sendNegative(fd);

                        } else {
                            send_status = sendNegative(fd);
                        }
                    }

                } else if (strcasecmp(command, "RETR") == 0) {
                    char* arg = retrieveArgs(reply);

                    if (arg && numbers_only(arg)) {
                        unsigned int index = (int)strtol(arg, NULL, 10);

                        if ((mail = get_mail_item(mailList, index - 1))) {
                            // call helper to read the email
                            send_status = readEmail(fd, mail);

                        } else {
                            send_status = sendNegative(fd);
                        }

                    } else {
                        send_status = sendNegative(fd);
                    }

                } else if (strcasecmp(command, "DELE") == 0) {
                    char* arg = retrieveArgs(reply);

                    // check if arguments only contain numbers
                    if (arg && numbers_only(arg)) {
                        unsigned int index = (int)strtol(arg, NULL, 10);

                        if ((mail = get_mail_item(mailList, index - 1))) {
                            mark_mail_item_deleted(mail);
                            send_status = sendPositive(fd);
                        } else {
                            send_status = sendNegative(fd);
                        }

                    } else {
                        send_status = sendNegative(fd);
                    }

                } else if (strcasecmp(command, "NOOP") == 0) {
                    send_status = sendPositive(fd);

                } else if (strcasecmp(command, "RSET") == 0 && reply_size == 6) {
                    reset_mail_list_deleted_flag(mailList);
                    send_status = sendCountPositive(fd, get_mail_count(mailList), get_mail_list_size(mailList));

                } else if (strcasecmp(command, "QUIT") == 0 && reply_size == 6) {
                    destroy_mail_list(mailList);
                    send_status = sendPositive(fd);
                    break;

                } else {
                    send_status = sendNegative(fd);
                }
            }

            else {
                // Should not ever reach this block
                send_status = sendNegative(fd);
            }

        } else {
            // reply did not terminate with CRLF, or contained trailing spaces
            send_status = sendNegative(fd);
        }

        // could not send message, closing connection
        if (send_status == -1) break;
    }

    // free allocated socketbuffer
    sb_destroy(buffer);
}
