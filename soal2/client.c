#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 9000
#define BUFSZ       4096

int main(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Connected to DB Server on port %d\n", SERVER_PORT);
    printf("Type HELP for available commands\n");
    printf("Type EXIT to quit\n");

    char line[BUFSZ];
    char reply[BUFSZ];

    for (;;) {
        printf("\ndb > ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            break;

        size_t n = strlen(line);
        if (n > 0 && line[n - 1] == '\n')
            line[n - 1] = '\0';

        if (strcmp(line, "EXIT") == 0)
            break;

        if (line[0] == '\0')
            continue;

        strncat(line, "\n", sizeof(line) - strlen(line) - 1);
        if (send(sock, line, strlen(line), 0) < 0) {
            perror("send");
            break;
        }

        int r = recv(sock, reply, sizeof(reply) - 1, 0);
        if (r <= 0)
            break;
        reply[r] = '\0';

        size_t rlen = strlen(reply);
        if (rlen > 0 && reply[rlen - 1] == '\n')
            reply[rlen - 1] = '\0';

        printf("\n%s\n", reply);
    }

    close(sock);
    return 0;
}
