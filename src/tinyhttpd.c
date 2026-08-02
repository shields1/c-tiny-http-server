#include "../src/include/tinyhttpd.h"

const char *get_content_type(const char *path) {
    const char *ext = strrchr(path, '.');

    if (ext == NULL) {
        return "application/octet-stream";
    }

    size_t count = sizeof(mime_types) / sizeof(mime_types[0]);

    for (size_t i = 0; i < count; i++) {
        if (strcmp(ext, mime_types[i].ext) == 0) {
            return mime_types[i].type;
        }
    }

    return "application/octet-stream";
}

int path_to_int(const char *path) {
    // printf("path is %s\n", path);
    if (strcmp(path, "/") == 0)
        return 1;
    if (strcmp(path, "/rocket.png") == 0)
        return 2;
    if (strcmp(path, "/fonts/iosevka-regular.woff") == 0)
        return 3;
    if (strcmp(path, "/fonts/iosevka-regular.woff2") == 0)
        return 4;
    return 0; // default case
}

const char *parse_path(const char *path) {
    int val = path_to_int(path);

    switch (val) {
    case 1:
        return "./static/index.html";
    case 2:
        return "./static/rocket.png";
    case 3:
        return "./static/fonts/iosevka-regular.woff";
    case 4:
        return "./static/fonts/iosevka-regular.woff2";
    }
    return NULL;
}

int parse_request(char *buf, size_t len, http_request *req) {
    memset(req, 0, sizeof(*req));

    char *line = buf;
    char *end = buf + len;

    int first_line = 1;

    while (line < end) {
        // Find end of current line
        char *next = strstr(line, "\r\n");
        if (next == NULL) {
            return -1;
        }
        size_t line_len = next - line;
        // Blank line = end of headers
        if (line_len == 0) {
            break;
        }

        if (first_line) {
            char request_line[512];

            if (line_len >= sizeof(request_line)) {
                return -1;
            }

            memcpy(request_line, line, line_len);
            request_line[line_len] = '\0';

            if (sscanf(request_line,
                       "%7s %255s %15s",
                       req->method,
                       req->path,
                       req->protocol) != 3) {
                return -1;
            }
            first_line = 0;
        } else {
            char header[1024];

            if (line_len >= sizeof(header)) {
                return -1;
            }

            memcpy(header, line, line_len);
            header[line_len] = '\0';

            char *colon = strchr(header, ':');

            if (colon == NULL) {
                line = next + 2;
                continue;
            }

            *colon = '\0';

            char *key = header;
            char *value = colon + 1;

            while (*value == ' ') {
                value++;
            }

            if (strcmp(key, "Host") == 0) {
                strncpy(req->host, value, sizeof(req->host) - 1);
            } else if (strcmp(key, "User-Agent") == 0) {
                strncpy(req->user_agent, value, sizeof(req->user_agent) - 1);
            } else if (strcmp(key, "Connection") == 0) {
                strncpy(req->connection, value, sizeof(req->connection) - 1);
            } else if (strcmp(key, "X-Forwarded-For") == 0) {
                strncpy(req->real_ip, value, sizeof(req->real_ip) - 1);
            }
        }
        line = next + 2;
    }
    return 0;
}

int send_all(int s, const char *buf, size_t len) {
    size_t total = 0; // how many bytes have we sent
    ssize_t n;

    while (total < len) {
        n = send(s, buf + total, len - total, 0);
        if (n <= 0) {
            return -1;
        }
        total += (size_t)n;
    }

    return 0;
}

void send_file(int sock_fd, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        const char *err = "HTTP/1.1 404 Not Found\r\n"
                          "Content-Length: 0\r\n"
                          "Connection: close\r\n"
                          "\r\n";
        send_all(sock_fd, err, strlen(err));
        return;
    }
    // find file size
    fseek(f, 0, SEEK_END);
    off_t size = ftello(f);
    fseek(f, 0, SEEK_SET);

    char header[256];
    const char *content_type = get_content_type(path);

    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "Server: tinyhttpd\r\n"
             "\r\n",
             content_type,
             size);
    size_t hlen = strlen(header);
    if (send_all(sock_fd, header, hlen) == -1) {
        fclose(f);
        return;
    }

    char buf[BUFFER_SIZE] = {0};
    size_t n_read = 0;
    while ((n_read = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (send_all(sock_fd, buf, n_read) == -1) {
            perror("Tiny > send");
            break;
        }
    }
    fclose(f);
}

void sigchld_handler(int s) {
    (void)s; // quite unused variable warning
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved_errno;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
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

    int rv, numbytes;
    // When running under systemd, stdout may be buffered.
    // This should fix this.
    setvbuf(stdout, NULL, _IONBF, 0);
    //*
    // Let Mail-in-a-box with nginx be infront of my server and forward traffic
    //                 Internet
    //                   |
    //                   |
    //             nginx :80/:443
    //         (Mail-in-a-Box managed)
    //                   |
    //                   |
    //            127.0.0.1:3490
    //                   |
    //                   |
    //             Tiny C server
    //*
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    // hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo("127.0.0.1", PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "tinyhttpd > getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("tinyhttpd > socket");
            continue;
        }

        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("Tiny > setsockopt");
            exit(EXIT_FAILURE);
        }

        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            perror("tinyhttpd > bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL) {
        fprintf(stderr, "tinyhttpd > failed to bind\n");
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

    printf("tinyhttpd > waiting for connections...\n");

    while (1) {
        sin_size = sizeof(their_addr);
        if ((new_fd = accept(sock_fd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof(s));
        printf("tinyhttpd > got connection from %s\n", s);
        if (!fork()) {
            close(sock_fd); // child does not need the listner

            if ((numbytes = recv(new_fd, buf, sizeof(buf) - 1, 0)) == -1) {
                perror("recv");
                exit(EXIT_FAILURE);
            }
            buf[numbytes] = '\0';
            // printf("server: received %d bytes\n'%s'\n", numbytes, buf);
            http_request req;
            if (parse_request(buf, numbytes, &req) != 0) {
                const char *err = "HTTP/1.1 400 Bad Request\r\n"
                                  "Content-Length: 0\r\n"
                                  "Connection: close\r\n"
                                  "\r\n";
                send_all(new_fd, (char *)err, strlen(err));
                close(new_fd);
                exit(0);
            }
            printf("Method      : %s\n", req.method);
            printf("Path        : %s\n", req.path);
            printf("Protocol    : %s\n", req.protocol);
            printf("Host        : %s\n", req.host);
            printf("Real IP     : %s\n", req.real_ip);
            printf("User-Agent  : %s\n", req.user_agent);
            printf("Connection  : %s\n", req.connection);

            if (strcmp(req.method, "GET") != 0) {
                const char *err = "HTTP/1.1 405 Method Not Allowed\r\n"
                                  "Content-Length: 0\r\n"
                                  "Connection: close\r\n"
                                  "\r\n";
                send_all(new_fd, (char *)err, strlen(err));
                close(new_fd);
                exit(0);
            }

            const char *file = parse_path(req.path);
            if (file == NULL) {
                const char *err =
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n"
                    "\r\n";
                send_all(new_fd, err, strlen(err));
            } else {
                send_file(new_fd, file);
            }
            close(new_fd);
            exit(0);
        }
        // parent closes original new_fd
        close(new_fd);
    }
    close(sock_fd);
    return 0;
}
