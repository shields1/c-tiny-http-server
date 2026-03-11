#include "src/include/tiny.h"

int route_to_int(const char *route) {
    //printf("Route is %s\n", route);
    if (strcmp(route, "/") == 0) return 1;
    if (strcmp(route, "/rocket.png") == 0) return 2;
    if (strcmp(route, "/fonts/iosevka-regular.woff") == 0) return 3;
    if (strcmp(route, "/fonts/iosevka-regular.woff2") == 0) return 4;
    return 0; // default case
}
char *parse_route(const char *route) {
    int val = route_to_int(route);

    switch (val) {
        case 1: return "./static/index.html";
        case 2: return "./static/rocket.png";
        case 3: return "./static/fonts/iosevka-regular.woff";
        case 4: return "./static/fonts/iosevka-regular.woff2";
    }
    return NULL;
}

int send_all(int s, char *buf, int *len) {
    int total = 0;         // how many bytes have we sent
    int bytes_left = *len; // how many we have left to send
    int n;

    while(total < *len) {
        n = send(s, buf+total, bytes_left, 0);
        if (n == -1) { break; }
        total += n;
        bytes_left -= n;
    }
    *len = total;          //return number actually sent header_len

    return n == -1 ? -1:0; // return -1 on failure, 0 on success
}

void send_file(int sock_fd, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        const char *err = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send_all(sock_fd, (char*)err, &(int){strlen(err)});
        close(sock_fd);
        return;
    }
    // find file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char header[256];
    snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "\r\n",
            strstr(path, ".png") ? "image/png" :
            strstr(path, ".woff") ? "font/woff" :
            "text/html",
            size);
    int hlen = (int)strlen(header); 
    if (send_all(sock_fd, header, &hlen) == -1) {
        fclose(f);
        close(sock_fd);
        return;
    }
    
    char buf[BUFFER_SIZE] = {0};
    int n_read = 0; 
    while ((n_read = fread(buf, sizeof(buf[0]), BUFFER_SIZE, f)) > 0) {
        if (send_all(sock_fd, buf, &n_read) == -1) {
            perror("send");
            break;
        }
    }
    fclose(f);
    close(sock_fd);
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
    char buf[BUFFER_SIZE + 1];
    char *token;
    char *method;
    char *route;
    char *protocol;
    int rv, numbytes;
    
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
        if (!fork()) {
        //    close(sock_fd); // child does not need the listner

        
            if ((numbytes = recv(new_fd, buf, sizeof(buf) - 1, 0)) == -1) {
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
            route = token;
            // fetch protocol
            token = strtok(NULL, "\n");
            protocol = token;

            printf("method: %s\nroute: %s\nprotocol: %s\n", method, route, protocol);
            //while(token != NULL) {
            //    printf(" %s\n", token);
            //    token = strtok(NULL, " ");
            //}
            if (strcmp(method, "GET") != 0) {
                printf("method not GET");
                continue;
            }
            printf("protocol is: %s\n", protocol);

            const char *file = parse_route(route);
            send_file(new_fd, file);
            // child closes his copy of new_fd
            close(new_fd);
            exit(0);
        }
        // parent closes original new_fd
        close(new_fd);
    }
        close(sock_fd);
    return 0;
}
