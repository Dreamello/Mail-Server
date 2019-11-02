#include "server.h"
#include "socketbuffer.h"
#include "user.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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
int sendWelcome(int fd, char* domainName) {
    char welcomeMessage[300] = "";
    strcat(welcomeMessage, "220 ");
    strcat(welcomeMessage, domainName);
    strcat(welcomeMessage, " SMTP Server Ready\r\n");
    return send_all(fd, welcomeMessage, strlen(welcomeMessage));
}

/** Sends a status 221 message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int send221(int fd) {
    return send_string(fd, "221 OK\r\n");
}

/** Sends a status 250 message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int send250(int fd) {
    return send_string(fd, "250 OK\r\n");
}

/** Sends a status 354 message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int send354(int fd) {
    return send_string(fd, "354 End data with <CRLF>.<CRLF>\r\n");
}

/** Sends a status 451 message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int send451(int fd) {
    return send_string(fd, "451 Requested action aborted: error in processing\r\n");
}

/** Sends a status 500 message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int send500(int fd) {
    return send_string(fd, "500 Syntax error, command unrecognized\r\n");
}

/** Sends a status 501 message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int send501(int fd) {
    return send_string(fd, "501 Syntax error in parameters or arguments\r\n");
}

/** Sends a status 502 message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int send502(int fd) {
    return send_string(fd, "502 Command not implemented\r\n");
}

/** Sends a status 503 message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int send503(int fd) {
    return send_string(fd, "503 Bad sequence of commands\r\n");
}

/** Sends a status 550 message to the given fd
 *
 *  Parameters: fd: Socket file descriptor.
 *
 *  Return: number of bytes if successfully sent, -1 if failed
 */
int send555(int fd) {
    return send_string(fd, "555 Recipient not recognized\r\n");
}

/** Converts a given string to uppercase
 *
 *  Parameters: temp: String to convert.
 */
void uppercase(char* temp) {
    char* s = temp;
    while (*s) {
        *s = toupper((unsigned char)*s);
        s++;
    }
}

/** Checks if given argument has the correct syntax for a MAIL command
 *
 *  Parameters: str: String to be checked
 *
 *  Return: 1 if syntax is correct, 0 otherwise
 */
int checkMailSyntax(char* str, int size) {
    char* openBracket;
    char* closeBracket;
    openBracket = strchr(str, '<');
    closeBracket = strrchr(str, '>');

    // check brackets are in order, with at least something in between
    if (openBracket && closeBracket && (int)(closeBracket - openBracket) > 1) {
        // check argument begins with FROM:<
        char from[7];
        sscanf(str, "%6s", from);
        if (strcasecmp(from, "FROM:<") == 0) {
            // check argument ends with >
            if (str[size - 1] == '>')
                return 1;
        }
    }
    return 0;
}

/** Checks if given argument has the correct syntax for a RCPT command
 *
 *  Parameters: str: String to be checked
 *
 *  Return: 1 if syntax is correct, 0 otherwise
 */
int checkRcptSyntax(char* str, int size) {
    char* openBracket;
    char* closeBracket;
    openBracket = strchr(str, '<');
    closeBracket = strrchr(str, '>');
    // check brackets are in order
    if (openBracket && closeBracket && (int)(closeBracket - openBracket) > 1) {
        // check argument begins with TO:<
        char to[5];
        sscanf(str, "%4s", to);
        if (strcasecmp(to, "TO:<") == 0) {
            // check argument ends with >
            if (str[size - 1] == '>')
                return 1;
        }
    }
    return 0;
}

/** Returns the starting char of given email
 *
 *  Parameters: str: String containing email
 *
 *  Return: the starting char of given email
 */
char* retrieveEmail(char* str) {
    // returns the next char after "<"
    char* ret = strchr(str, '<');
    char* closeBracket = strrchr(str, '>');
    *closeBracket = '\0';
    return ++ret;
}

/** Checks if given string ends with CRLF, or contains any trailing spaces, or only contains CRLF
 *
 *  Parameters: str: String to be checked
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

/** Checks if given string ends with CRLF, used during DATA transmissing, where trailing spaces
 *          and empty lines are acceptible.
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

/** Saves given email as temp file, then saves the temp file into the corresponding
 *    user's mail.store folder.
 *
 *  Return: -1 if error occured, fd of temp file otherwise.
 */
int saveEmail(char fromEmail[], char toEmail[], char* data) {
    char template[] = "tmpXXXXXX";
    int tempfd = mkstemp(template);
    if (tempfd != -1) {
        write(tempfd, data, strlen(data));
        user_list_t list = create_user_list();
        add_user_to_list(&list, toEmail);
        save_user_mail(template, list);
        destroy_user_list(list);
        unlink(template);
        close(tempfd);
    }
    return tempfd;
}

void handle_client(int fd) {
    // TODO To be implemented

    // INIT
    int reply_size;
    int send_status = 1;
    int received_helo = 0;
    int received_mail = 0;
    int received_rcpt = 0;
    int received_data = 0;
    int rcpt_count = 0;
    char* data;
    char fromEmail[MAX_USERNAME_SIZE + 1];
    char toEmail[30][MAX_USERNAME_SIZE + 1];
    char command[41];
    char reply[MAX_LINE_LENGTH + 1];
    socket_buffer_t buffer = sb_create(fd, MAX_LINE_LENGTH);

    // send welcome message
    char domainName[201];
    gethostname(domainName, 200);
    send_status = sendWelcome(fd, domainName);

    // TODO: If send fails, server should terminate this client connection
    while (1) {
        reply_size = sb_read_line(buffer, reply);

        if (reply_size == 0 || reply_size == -1 || send_status == -1) break;

        sscanf(reply, "%s", command);

        // if command ends with CRLF and does not contain trailing whitespace.
        // if we are in the middle of a DATA transmission, then we only check for ending CRLF
        if (checkCRLF(reply, reply_size) || (received_data && checkCRLFSimple(reply, reply_size))) {
            // STATE: DATA received
            if (received_data) {
                // Must be terminated using <CRLF>.<CRLF>
                if (strstr(reply, ".\r\n") == reply && reply_size == 3) {
                    // marks the completion of a transaction
                    int success = 0;
                    for (int i = 0; i < rcpt_count; i++) {
                        success = saveEmail(fromEmail, toEmail[i], data);
                    }

                    free(data);
                    received_data = 0;
                    rcpt_count = 0;

                    if (success == -1)
                        send_status = send451(fd);
                    else
                        send_status = send250(fd);

                } else {
                    strcat(data, reply);
                }
            }

            // Commands accepted at every state except DATA
            else if (strcasecmp(command, "NOOP") == 0) {
                send_status = send250(fd);

            } else if (strcasecmp(command, "QUIT") == 0) {
                send_status = send221(fd);
                break;

            } else if (strcasecmp(command, "EHLO") == 0 ||
                       strcasecmp(command, "RSET") == 0 ||
                       strcasecmp(command, "VRFY") == 0 ||
                       strcasecmp(command, "EXPN") == 0 ||
                       strcasecmp(command, "HELP") == 0) {
                send_status = send502(fd);  // not implemented

            }

            // STATE: MAIL received
            else if (received_mail) {
                if (strcasecmp(command, "RCPT") == 0) {
                    char* rcptArgs = retrieveArgs(reply);

                    if (rcptArgs && checkRcptSyntax(rcptArgs, (int)strlen(rcptArgs))) {
                        char* email = retrieveEmail(rcptArgs);

                        if (is_valid_user(email, NULL)) {
                            // valid user, store email, increment rcpt count
                            strcpy(toEmail[rcpt_count++], email);
                            received_rcpt = 1;
                            received_mail = 0;
                            send_status = send250(fd);

                        } else {
                            // invalid user
                            send_status = send555(fd);
                        }

                    } else {
                        send_status = send501(fd);  // Syntax Error
                    }

                } else if (strcasecmp(command, "HELO") == 0 ||
                           strcasecmp(command, "MAIL") == 0 ||
                           strcasecmp(command, "DATA") == 0) {
                    send_status = send503(fd);

                } else {
                    send_status = send500(fd);
                }

            }

            // STATE: RCPT received
            else if (received_rcpt) {
                if (strcasecmp(command, "DATA") == 0 && reply_size == 6) {
                    received_data = 1;
                    received_rcpt = 0;
                    // RFC5321 section 4.5.3.1.7. specifies at least 64K octets for message content
                    data = malloc((64000 * sizeof(char)) + 1);
                    strcpy(data, "");
                    send_status = send354(fd);

                } else if (strcasecmp(command, "RCPT") == 0) {
                    char* rcptArgs = retrieveArgs(reply);

                    if (rcptArgs && checkRcptSyntax(rcptArgs, (int)strlen(rcptArgs))) {
                        // Correct syntax
                        char* email = retrieveEmail(rcptArgs);

                        if (is_valid_user(email, NULL)) {
                            strcpy(toEmail[rcpt_count++], email);
                            send_status = send250(fd);
                        } else {
                            send_status = send555(fd);
                        }

                    } else {
                        send_status = send501(fd);  // Syntax Error
                    }

                } else if (strcasecmp(command, "HELO") == 0 ||
                           strcasecmp(command, "MAIL") == 0) {
                    send_status = send503(fd);

                } else {
                    send_status = send500(fd);
                }

            }

            // STATE: HELO received
            else if (received_helo) {
                if (strcasecmp(command, "MAIL") == 0) {
                    char* mailArgs = retrieveArgs(reply);

                    if (mailArgs && checkMailSyntax(mailArgs, (int)strlen(mailArgs))) {
                        received_mail = 1;
                        char* email = retrieveEmail(mailArgs);

                        // store email
                        strcpy(fromEmail, email);
                        send_status = send250(fd);

                    } else {
                        send_status = send501(fd);  // Syntax Error
                    }

                } else if (strcasecmp(command, "HELO") == 0 ||
                           strcasecmp(command, "RCPT") == 0 ||
                           strcasecmp(command, "DATA") == 0) {
                    send_status = send503(fd);

                } else {
                    send_status = send500(fd);
                }

            }

            // STATE: initial
            else {
                if (strcasecmp(command, "HELO") == 0) {
                    received_helo = 1;
                    char initialMessage[200] = "";
                    strcat(initialMessage, "250 ");
                    strcat(initialMessage, domainName);
                    strcat(initialMessage, "\r\n");
                    send_status = send_all(fd, initialMessage, strlen(initialMessage));

                } else if (strcasecmp(command, "MAIL") == 0 ||
                           strcasecmp(command, "RCPT") == 0 ||
                           strcasecmp(command, "DATA") == 0) {
                    send_status = send503(fd);

                } else {
                    send_status = send500(fd);
                }
            }
        }

        // reply does not end with CRLF, also catches line too long errors
        else {
            send_status = send500(fd);
        }

        // could not send message, closing connection
        if (send_status == -1) break;
    }

    sb_destroy(buffer);
}
