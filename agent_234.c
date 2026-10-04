#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define SID "4322"
#define AUTH_TOKEN "OPS-2234"

/* Receive one line ending with '\n' */
int receive_line(int socket_fd, char *buffer, int buffer_size)
{
    int total = 0;
    char ch;

    while (total < buffer_size - 1)
    {
        int n = recv(socket_fd, &ch, 1, 0);

        if (n <= 0)
        {
            return n;
        }

        buffer[total++] = ch;

        if (ch == '\n')
        {
            break;
        }
    }

    buffer[total] = '\0';

    return total;
}

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("RemoteOps Agent started\n");
    printf("SID: %s\n", SID);
    printf("Listening on TCP port %d\n", PORT);

    while (1)
    {
        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_addr,
                           &client_len);

        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        printf("Controller connected\n");

        /* Ask Controller to authenticate */
        const char *auth_request = "SID:4322 AUTH REQUIRED\n";

        send(client_fd,
             auth_request,
             strlen(auth_request),
             0);

        /* Receive authentication token */
        char buffer[1024];

        int bytes_received = receive_line(client_fd,
                                          buffer,
                                          sizeof(buffer));

        if (bytes_received <= 0)
        {
            printf("Authentication failed: no response\n");
            close(client_fd);
            continue;
        }

        /* Check authentication */
        char expected[100];

        snprintf(expected,
                 sizeof(expected),
                 "AUTH %s\n",
                 AUTH_TOKEN);

        if (strcmp(buffer, expected) == 0)
        {
            const char *auth_ok = "SID:4322 AUTH OK\n";

            send(client_fd,
                 auth_ok,
                 strlen(auth_ok),
                 0);

            printf("Controller authenticated successfully\n");
        }
        else
        {
            const char *auth_failed = "SID:4322 AUTH FAILED\n";

            send(client_fd,
                 auth_failed,
                 strlen(auth_failed),
                 0);

            printf("Controller authentication failed\n");
        }

        close(client_fd);

        printf("Controller disconnected\n");
    }

    close(server_fd);

    return 0;
}
