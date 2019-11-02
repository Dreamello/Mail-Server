/*
 * Creates a buffer for receiving data from a socket and reading individual lines.
 */

#include "socketbuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

struct socket_buffer {
    int fd;
    size_t max_bytes;
    size_t avail_data;
    // Buffer set as size zero, but since it's the last member of the
    // struct, any additional memory allocated after this struct can be
    // used as part of the buffer.
    char buf[0];
};

/** Creates a new buffer for handling data read from a socket.
 *
 *  Note: The maximum buffer size passed as parameter will also
 *  correspond to the maximum number of bytes other functions (like
 *  sb_read_line) can return at a time, so it is advisable to make
 *  this size at least as big as the maximum line size for the
 *  protocol handled in this socket.
 *  
 *  Parameters: fd: Socket file descriptor.
 *              max_buffer_size: Maximum number of bytes to be stored
 *                               locally for a connection. 
 *
 *  Returns: A socket_buffer_t object that can be used in other functions
 *           to read buffered data.
 */
socket_buffer_t sb_create(int fd, size_t max_buffer_size) {
    socket_buffer_t sb = malloc(sizeof(struct socket_buffer) + max_buffer_size);
    sb->fd = fd;
    sb->max_bytes = max_buffer_size;
    sb->avail_data = 0;
    return sb;
}

/** Frees all memory used by a socket_buffer_t object.
 *  
 *  Parameters: sb: buffer object to be freed.
 */
void sb_destroy(socket_buffer_t sb) {
    free(sb);
}

/** Reads a single line from the socket/buffer. If the socket returns
 *  more than one line in a single call to recv, returns a single line
 *  and caches the remaining data for the next call. The returned
 *  string will also include a null byte, which allows the out buffer
 *  to the handled as a regular string.
 *
 *  If a line with more than max_buffer_size bytes is read, then
 *  return the first max_buffer_size bytes (with a terminating null
 *  byte). It is the responsibility of the caller to check if the last
 *  character in the string is a line-feed (\n) character.
 *
 *  This function does not check for null bytes found in the middle of
 *  the string.
 *
 *  Parameter: sb: buffer object where socket and cache data are stored.
 *             out: array of bytes where the read line will be
 *                  stored. It must have space for at least
 *                  max_buffer_size bytes (from sb_create function)
 *                  plus one (for terminating null byte).
 *
 *  Returns: If the connection was terminated properly, returns 0. If
 *           the connection was terminated abruptly or another unknown
 *           error is found, returns -1. Otherwise, returns the number
 *           of bytes in the read line.
 */
int sb_read_line(socket_buffer_t sb, char out[]) {
    char *eos;
    int rv;
    while ((eos = memchr(sb->buf, '\n', sb->avail_data)) == NULL) {
        if (sb->avail_data < sb->max_bytes) {
            rv = recv(sb->fd, sb->buf + sb->avail_data, sb->max_bytes - sb->avail_data, 0);
            if (rv < 0)
                return rv;
            if (rv == 0) {
                eos = sb->buf + sb->avail_data - 1;
                break;
            }
            sb->avail_data += rv;
        } else {
            eos = sb->buf + sb->max_bytes - 1;
            break;
        }
    }

    rv = eos - sb->buf + 1;
    memcpy(out, sb->buf, rv);
    out[rv] = 0;
    sb->avail_data -= rv;
    if (sb->avail_data)
        memmove(sb->buf, eos + 1, sb->avail_data);
    return rv;
}
