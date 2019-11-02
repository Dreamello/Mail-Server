/*
 * Creates a buffer for receiving data from a socket and reading individual lines.
 */

#ifndef _SOCKET_BUFFER_H_
#define _SOCKET_BUFFER_H_

#include <string.h>

typedef struct socket_buffer *socket_buffer_t;

socket_buffer_t sb_create(int fd, size_t max_buffer_size);
void sb_destroy(socket_buffer_t sb);
int sb_read_line(socket_buffer_t sb, char out[]);

#endif
