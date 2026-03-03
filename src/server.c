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
#define MAXLEN 1024

int send_all(int s, char *buf, int *len) {
    int total = 0; // how many bytes have we sent
    int bytes_left = *len; // how many we have left to send
    int n;

    while(total < *len) {
        n = send(s, buf+total, bytes_left, 0);
        if (n == 1) { break; }
        total += n;
        bytes_left -= n;
    }
    *len = total; //return number actually sent header_len

    return n == -1 ? -1:0;// return -1 on failure, 0 on success
}
void sigchld_handler(int s) {

    (void)s; // quite unused variable warning

    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main() {
    int sock_fd, new_fd; // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    char buf[1024];
    char *token;
    char *method;
    char *uri;
    char *protocol;
    char *response;
    const char html_header[] = "HTTP/1.1 200 OK\n"
                         "Content-Type: text/html; charset=UFT-8\n"
                         "Date: Fri, 21 Jun 2024 14:18:33 GMT\n"
                         "Last-Modified: Thu, 17 Oct 2019 07:18:26 GMT\n"
                         "Content-Length: %d\n"
                         "\n%s\n";
    const char html[] = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>Hello World</title></head><body><h1>Hello World</h1></body></html>";
    
    int rv, numbytes, response_length;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    
    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(EXIT_FAILURE);
        }

        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            perror("server: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo); //all done with this structure

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(EXIT_FAILURE);
    }

    if (listen(sock_fd, BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while (1) {
        sin_size = sizeof(their_addr);
        if ((new_fd = accept(sock_fd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof(s));
        printf("server: got connection from %s\n", s);

        if ((numbytes = recv(new_fd, buf, 1024, 0)) == -1) {
            perror("recv");
            exit(EXIT_FAILURE);
        }
        buf[numbytes] = '\0';
        printf("server: received %d bytes\n'%s'\n",numbytes, buf); 
        
        // fetch HTTP method
        token = strtok(buf, " ");
        method = token; 
        // fetch uri 
        token = strtok(NULL, " ");
        uri = token;
        // fetch protocol
        token = strtok(NULL, "\n");
        protocol = token;
        
        printf("method: %s\nuri: %s\nprotocol: %s\n", method, uri, protocol);
        /*while(token != NULL) {
            printf(" %s\n", token);
            token = strtok(NULL, " ");
        }*/
        if (strcmp(method, "GET") != 0) {
            printf("method not GET");
        }
        printf("protocol is: %s\n", protocol);

        int header_len = strlen(html_header);
        int body_len = strlen(html);
        size_t total_len = (size_t)header_len + body_len;
        response = malloc(total_len + 1);
        if(!response) {
            perror("malloc response");
            return EXIT_FAILURE;
        } 

        response_length = snprintf(response,
                                   MAXLEN,
                                   "HTTP/1.1 200 OK\n"
                                   "Content-Type: text/html; charset=UFT-8\n"
                                   "Date: Fri, 21 Jun 2024 14:18:33 GMT\n"
                                   "Last-Modified: Thu, 17 Oct 2019 07:18:26 GMT\n"
                                   "Content-Length: %d\n"
                                   "\n%s\n",
                                   response_length, html);
        printf("Length to send is: %d\n", response_length);
        printf("sending:\n%s\n", response);

        if (send_all(new_fd, response, &response_length) == -1) {
            perror("send");
            printf("We opnly send %d bytes because of the error!\n", response_length);
        }

        close(new_fd);
        free(response);    
    }
        close(sock_fd);
    return 0;
}
