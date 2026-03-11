#ifndef TINY_H
#define TINY_H
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

#define PORT "3490"
#define BACKLOG 10
#define BUFFER_SIZE 1024

int route_to_int(const char *route);
char *parse_route(const char *route);
int send_all(int s, char *buf, int *len);
void send_file(int sock_fd, const char *path);
void sigchld_handler(int s);
void *get_in_addr(struct sockaddr *sa);

#endif
