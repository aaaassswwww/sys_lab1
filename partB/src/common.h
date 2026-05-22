#ifndef PARTB_COMMON_H
#define PARTB_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

void die(const char *message);
void die_errno(const char *message);
void ignore_sigpipe(void);
void set_nonblocking(int fd);
int create_listen_socket(uint16_t port, int backlog, int nonblocking);
int connect_to_server(const char *host, uint16_t port);
uint16_t parse_port(const char *text);
int parse_int(const char *text, const char *name, int min_value);
size_t parse_size(const char *text, const char *name, size_t min_value);
uint64_t monotonic_ns(void);
ssize_t write_full(int fd, const void *buffer, size_t length);
ssize_t read_full(int fd, void *buffer, size_t length);

#endif
