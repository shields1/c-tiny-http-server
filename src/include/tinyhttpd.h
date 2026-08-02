#ifndef TINY_HTTPD_H
#define TINY_HTTPD_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>

#define PORT "3490"
#define BACKLOG 10
#define BUFFER_SIZE 1024

volatile sig_atomic_t running = 1;

struct mime_map {
    const char *ext;
    const char *type;
};

typedef struct http_request {
    char method[8];
    char path[256];
    char protocol[16];

    char host[256];
    char real_ip[64];
    char user_agent[256];
    char connection[32];
} http_request;

static const struct mime_map mime_types[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
};

int path_to_int(const char *);
const char *parse_path(const char *);
int parse_request(char *, size_t, http_request *);
int send_all(int, const char *, size_t);
void send_file(int sock_fd, const char *);
void sigterm_handler(int);
const char *inet_ntop2(void *, char *, size_t);
int get_listener_socket(void);
void handle_new_connection(int, fd_set *, int *);
void handle_client_data(int, fd_set *);
void *get_in_addr(struct sockaddr *);
const char *get_content_type(const char *);

#endif
