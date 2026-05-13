#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 9000
#define BUFFER_SIZE 4096

static int send_all(int sockfd, const char *buf, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(sockfd, buf + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("Connected to DB Server on port %d\n", port);
    printf("Type HELP or another command. Press Ctrl+D to exit.\n\n");

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sockfd, &readfds);

        int maxfd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(sockfd, &readfds)) {
            char buf[BUFFER_SIZE];
            ssize_t n = recv(sockfd, buf, sizeof(buf) - 1, 0);

            if (n <= 0) {
                printf("\nConnection closed by server.\n");
                break;
            }

            buf[n] = '\0';
            printf("%s", buf);
            fflush(stdout);
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[BUFFER_SIZE];
            ssize_t n = read(STDIN_FILENO, input, sizeof(input));

            if (n <= 0) {
                break;
            }

            if (send_all(sockfd, input, n) < 0) {
                perror("send");
                break;
            }
        }
    }

    close(sockfd);
    return 0;
}
