#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9410
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
    int sock;
    struct sockaddr_in server_addr;
    char buffer[1024];

    /* Create TCP socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
    {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    /* Connect to Agent */
    if (connect(sock,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Connected to RemoteOps Agent\n");

    /* Receive authentication request */
    int bytes_received = receive_line(sock,
                                      buffer,
                                      sizeof(buffer));

    if (bytes_received <= 0)
    {
        printf("Failed to receive authentication request\n");
        close(sock);
        return 1;
    }

    printf("Agent: %s", buffer);

    /* Send authentication token */
    char auth_message[100];

    snprintf(auth_message,
             sizeof(auth_message),
             "AUTH %s\n",
             AUTH_TOKEN);

    send(sock,
         auth_message,
         strlen(auth_message),
         0);

    printf("Sent authentication token\n");

    /* Receive authentication result */
    memset(buffer, 0, sizeof(buffer));

    bytes_received = receive_line(sock,
                                  buffer,
                                  sizeof(buffer));

    if (bytes_received <= 0)
    {
        printf("Failed to receive authentication result\n");
        close(sock);
        return 1;
    }

    printf("Agent: %s", buffer);

    close(sock);

    return 0;
}
