#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9410

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

    /* Clear server address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    /* Convert IP address */
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
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

    /* Receive Agent response */
    memset(buffer, 0, sizeof(buffer));

    int bytes_received = recv(sock,
                              buffer,
                              sizeof(buffer) - 1,
                              0);

    if (bytes_received < 0)
    {
        perror("recv");
        close(sock);
        return 1;
    }

    buffer[bytes_received] = '\0';

    printf("Agent response: %s", buffer);

    close(sock);

    return 0;
}
