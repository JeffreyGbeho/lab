#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(8080);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(listen_fd, 16) < 0) {
        perror("listen");
        exit(1);
    }

    printf("listening on port 8080 (listen_fd = %d)\n", listen_fd);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (conn_fd < 0) {
            perror("accept");
            continue;
        }

        printf("client connected from %s:%d (conn_fd = %d)\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               conn_fd);

        char buf[4096];
        ssize_t n = read(conn_fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read");
            close(conn_fd);
            continue;
        }
        buf[n] = '\0';

        printf("read() returned %zd bytes\n", n);

        char *sp1 = strchr(buf, ' ');
        char *sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL;
        char *eol = sp2 ? strstr(sp2 + 1, "\r\n") : NULL;

        if (!sp1 || !sp2 || !eol) {
            fprintf(stderr, "malformed request line\n");
            close(conn_fd);
            continue;
        }

        char method[16];
        char path[2048];
        char version[16];

        size_t method_len  = (size_t)(sp1 - buf);
        size_t path_len    = (size_t)(sp2 - sp1 - 1);
        size_t version_len = (size_t)(eol - sp2 - 1);

        if (method_len >= sizeof(method) ||
            path_len >= sizeof(path) ||
            version_len >= sizeof(version)) {
            fprintf(stderr, "request line token too long\n");
            close(conn_fd);
            continue;
        }

        memcpy(method, buf, method_len);
        method[method_len] = '\0';
        memcpy(path, sp1 + 1, path_len);
        path[path_len] = '\0';
        memcpy(version, sp2 + 1, version_len);
        version[version_len] = '\0';

        printf("method=%s path=%s version=%s\n", method, path, version);

        const char *body = "Hello, world!\n";
        char response[4096];
        int response_len = snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %zu\r\n"
            "\r\n"
            "%s",
            strlen(body), body);

        ssize_t written = write(conn_fd, response, response_len);
        if (written < 0) {
            perror("write");
        } else {
            printf("wrote %zd of %d bytes\n", written, response_len);
        }

        close(conn_fd);
        printf("----- waiting for next client -----\n");
    }

    close(listen_fd);
    return 0;
}
