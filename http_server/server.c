#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void send_error(int conn_fd, int code, const char *reason) {
    char resp[128];
    int len = snprintf(resp, sizeof(resp),
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",
        code, reason);
    write(conn_fd, resp, len);
}

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
            send_error(conn_fd, 400, "Bad Request");
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
            send_error(conn_fd, 400, "Bad Request");
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

        struct { char name[64]; char value[256]; } headers[32];
        int header_count = 0;
        int header_error = 0;
        int header_status = 400;
        char *hp = eol + 2;

        while (hp + 1 < buf + n && !(hp[0] == '\r' && hp[1] == '\n')) {
            char *line_end = strstr(hp, "\r\n");
            if (!line_end) {
                fprintf(stderr, "malformed header (no CRLF)\n");
                header_error = 1;
                break;
            }

            char *colon = memchr(hp, ':', (size_t)(line_end - hp));
            if (!colon) {
                fprintf(stderr, "malformed header (no colon)\n");
                header_error = 1;
                break;
            }

            char *vp = colon + 1;
            while (vp < line_end && *vp == ' ') vp++;

            size_t name_len  = (size_t)(colon - hp);
            size_t value_len = (size_t)(line_end - vp);

            if (header_count >= 32 ||
                name_len >= sizeof(headers[0].name) ||
                value_len >= sizeof(headers[0].value)) {
                fprintf(stderr, "too many headers or a header too large\n");
                header_error = 1;
                header_status = 431;
                break;
            }

            memcpy(headers[header_count].name, hp, name_len);
            headers[header_count].name[name_len] = '\0';
            memcpy(headers[header_count].value, vp, value_len);
            headers[header_count].value[value_len] = '\0';
            header_count++;

            hp = line_end + 2;
        }

        if (header_error || !(hp + 1 < buf + n) || !(hp[0] == '\r' && hp[1] == '\n')) {
            fprintf(stderr, "incomplete or malformed headers\n");
            send_error(conn_fd, header_status,
                       header_status == 431 ? "Request Header Fields Too Large" : "Bad Request");
            close(conn_fd);
            continue;
        }

        char *body_start = hp + 2;

        printf("parsed %d headers:\n", header_count);
        for (int i = 0; i < header_count; i++) {
            printf("  %s: %s\n", headers[i].name, headers[i].value);
        }

        const char *host = NULL;
        size_t content_length = 0;
        for (int i = 0; i < header_count; i++) {
            if (strcasecmp(headers[i].name, "Host") == 0) {
                host = headers[i].value;
            } else if (strcasecmp(headers[i].name, "Content-Length") == 0) {
                content_length = (size_t)strtoul(headers[i].value, NULL, 10);
            }
        }
        printf("Host header = %s\n", host ? host : "(none)");

        size_t header_bytes = (size_t)(body_start - buf);
        size_t available = (n >= (ssize_t)header_bytes) ? (size_t)n - header_bytes : 0;
        size_t body_len = content_length < available ? content_length : available;

        printf("Content-Length = %zu, body bytes available in this read = %zu\n",
               content_length, available);
        if (content_length > available) {
            fprintf(stderr,
                "body incomplete in this read (have %zu of %zu) - not handled until the read loop\n",
                available, content_length);
        }
        if (body_len > 0) {
            printf("body: %.*s\n", (int)body_len, body_start);
        }

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
