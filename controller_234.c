/*
 * RemoteOps - Controller
 * Registration: IT24102234
 * SID: 4322
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9410

#define UDP_PORT 9500

#define AUTH_TOKEN "OPS-2234"
#define SID "4322"

#define TEST_FILE "test.txt"
#define DOWNLOAD_FILE "downloaded_test.txt"

#define BUFFER_SIZE 4096
#define LINE_SIZE 1024


/* ============================================================
   Send exactly all requested bytes
   ============================================================ */

int send_all(int sockfd, const void *buffer, size_t length)
{
    size_t total_sent = 0;
    const char *data = (const char *)buffer;

    while (total_sent < length)
    {
        ssize_t sent = send(sockfd,
                            data + total_sent,
                            length - total_sent,
                            0);

        if (sent <= 0)
        {
            return -1;
        }

        total_sent += sent;
    }

    return 0;
}


/* ============================================================
   Receive exactly the requested number of bytes
   ============================================================ */

int receive_all(int sockfd, void *buffer, size_t length)
{
    size_t total_received = 0;
    char *data = (char *)buffer;

    while (total_received < length)
    {
        ssize_t received = recv(sockfd,
                                data + total_received,
                                length - total_received,
                                0);

        if (received <= 0)
        {
            return -1;
        }

        total_received += received;
    }

    return 0;
}


/* ============================================================
   Receive one line ending with newline
   ============================================================ */

int receive_line(int sockfd, char *buffer, size_t size)
{
    size_t index = 0;

    while (index < size - 1)
    {
        char c;

        ssize_t received = recv(sockfd, &c, 1, 0);

        if (received <= 0)
        {
            return -1;
        }

        if (c == '\n')
        {
            buffer[index] = '\0';
            return 0;
        }

        if (c != '\r')
        {
            buffer[index++] = c;
        }
    }

    buffer[size - 1] = '\0';
    return 0;
}


/* ============================================================
   Connect to Agent
   ============================================================ */

int connect_to_agent(void)
{
    int sockfd;

    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return -1;
    }

    return sockfd;
}


/* ============================================================
   Authentication
   ============================================================ */

int authenticate(int sockfd)
{
    char response[LINE_SIZE];

    printf("\n--- AUTHENTICATION ---\n");

    printf("Sending authentication token: %s\n", AUTH_TOKEN);

    char request[LINE_SIZE];

    snprintf(request,
             sizeof(request),
             "AUTH %s\n",
             AUTH_TOKEN);

    if (send_all(sockfd, request, strlen(request)) < 0)
    {
        perror("send");
        return -1;
    }

    if (receive_line(sockfd, response, sizeof(response)) < 0)
    {
        printf("Failed to receive authentication response.\n");
        return -1;
    }

    printf("Agent: %s\n", response);

    if (strstr(response, "OK AUTHENTICATED") != NULL)
    {
        printf("Authentication successful.\n");
        return 0;
    }

    printf("Authentication failed.\n");

    return -1;
}


/* ============================================================
   Simple TCP command
   ============================================================ */

int send_command(int sockfd, const char *command)
{
    char request[LINE_SIZE];
    char response[BUFFER_SIZE];

    snprintf(request,
             sizeof(request),
             "%s\n",
             command);

    printf("\nSent: %s\n", command);

    if (send_all(sockfd, request, strlen(request)) < 0)
    {
        perror("send");
        return -1;
    }

    if (receive_line(sockfd, response, sizeof(response)) < 0)
    {
        printf("Failed to receive response.\n");
        return -1;
    }

    printf("Agent: %s\n", response);

    return 0;
}


/* ============================================================
   PUT
   ============================================================ */

int put_file(int sockfd)
{
    FILE *file;

    long filesize;

    char *data;

    char request[LINE_SIZE];

    char response[LINE_SIZE];

    file = fopen(TEST_FILE, "rb");

    if (file == NULL)
    {
        perror("fopen test.txt");
        return -1;
    }

    fseek(file, 0, SEEK_END);

    filesize = ftell(file);

    fseek(file, 0, SEEK_SET);

    if (filesize < 0)
    {
        fclose(file);
        return -1;
    }

    data = malloc(filesize);

    if (data == NULL)
    {
        printf("Memory allocation failed.\n");
        fclose(file);
        return -1;
    }

    if (fread(data, 1, filesize, file) != (size_t)filesize)
    {
        printf("Failed to read test file.\n");

        free(data);
        fclose(file);

        return -1;
    }

    fclose(file);

    snprintf(request,
             sizeof(request),
             "PUT %s %ld\n",
             TEST_FILE,
             filesize);

    printf("\nSent: PUT %s %ld\n",
           TEST_FILE,
           filesize);

    if (send_all(sockfd, request, strlen(request)) < 0)
    {
        free(data);
        return -1;
    }

    if (send_all(sockfd, data, filesize) < 0)
    {
        free(data);
        return -1;
    }

    printf("Sent %ld raw file bytes\n", filesize);

    free(data);

    if (receive_line(sockfd, response, sizeof(response)) < 0)
    {
        printf("Failed to receive PUT response.\n");
        return -1;
    }

    printf("Agent: %s\n", response);

    return 0;
}


/* ============================================================
   GET
   ============================================================ */

int get_file(int sockfd)
{
    char request[LINE_SIZE];

    char response[LINE_SIZE];

    char filename[256];

    long filesize;

    char *data;

    snprintf(request,
             sizeof(request),
             "GET %s\n",
             TEST_FILE);

    printf("\nSent: GET %s\n", TEST_FILE);

    if (send_all(sockfd, request, strlen(request)) < 0)
    {
        return -1;
    }

    if (receive_line(sockfd, response, sizeof(response)) < 0)
    {
        printf("Failed to receive GET response.\n");
        return -1;
    }

    printf("Agent: %s\n", response);

    if (sscanf(response,
               "OK FILE_SEND %255s %ld",
               filename,
               &filesize) != 2)
    {
        printf("Invalid GET response.\n");
        return -1;
    }

    data = malloc(filesize);

    if (data == NULL)
    {
        printf("Memory allocation failed.\n");
        return -1;
    }

    if (receive_all(sockfd, data, filesize) < 0)
    {
        printf("Failed to receive file data.\n");

        free(data);

        return -1;
    }

    printf("Received %ld raw file bytes\n", filesize);

    FILE *file = fopen(DOWNLOAD_FILE, "wb");

    if (file == NULL)
    {
        perror("fopen downloaded file");

        free(data);

        return -1;
    }

    fwrite(data, 1, filesize, file);

    fclose(file);

    free(data);

    printf("Saved downloaded file as %s\n",
           DOWNLOAD_FILE);

    return 0;
}


/* ============================================================
   UDP monitoring
   ============================================================ */

int start_udp_monitor(int tcp_sockfd)
{
    int udp_sockfd;

    struct sockaddr_in udp_addr;

    char request[LINE_SIZE];

    char response[LINE_SIZE];

    char udp_buffer[BUFFER_SIZE];

    struct sockaddr_in sender_addr;

    socklen_t sender_length;

    udp_sockfd = socket(AF_INET,
                        SOCK_DGRAM,
                        0);

    if (udp_sockfd < 0)
    {
        perror("UDP socket");
        return -1;
    }

    memset(&udp_addr, 0, sizeof(udp_addr));

    udp_addr.sin_family = AF_INET;

    udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    udp_addr.sin_port = htons(UDP_PORT);

    if (bind(udp_sockfd,
             (struct sockaddr *)&udp_addr,
             sizeof(udp_addr)) < 0)
    {
        perror("UDP bind");

        close(udp_sockfd);

        return -1;
    }

    printf("\n--- UDP MONITORING ---\n");

    printf("UDP listening port: %d\n",
           UDP_PORT);

    snprintf(request,
             sizeof(request),
             "MONITOR START %d\n",
             UDP_PORT);

    printf("Sent: MONITOR START %d\n",
           UDP_PORT);

    if (send_all(tcp_sockfd,
                 request,
                 strlen(request)) < 0)
    {
        close(udp_sockfd);

        return -1;
    }

    if (receive_line(tcp_sockfd,
                     response,
                     sizeof(response)) < 0)
    {
        printf("Failed to receive MONITOR START response.\n");

        close(udp_sockfd);

        return -1;
    }

    printf("Agent: %s\n", response);

    if (strstr(response,
               "OK MONITOR_STARTED") == NULL)
    {
        printf("Monitoring did not start.\n");

        close(udp_sockfd);

        return -1;
    }

    printf("\nWaiting for UDP monitoring packets...\n");

    /*
     * Receive approximately 3 monitoring packets.
     * The Agent sends one every 2 seconds.
     */

    for (int i = 0; i < 3; i++)
    {
        sender_length = sizeof(sender_addr);

        ssize_t received = recvfrom(
            udp_sockfd,
            udp_buffer,
            sizeof(udp_buffer) - 1,
            0,
            (struct sockaddr *)&sender_addr,
            &sender_length
        );

        if (received < 0)
        {
            perror("recvfrom");
            break;
        }

        udp_buffer[received] = '\0';

        printf("UDP Monitor: %s\n",
               udp_buffer);
    }

    /*
     * Stop monitoring through TCP.
     */

    printf("\nSent: MONITOR STOP\n");

    if (send_all(tcp_sockfd,
                 "MONITOR STOP\n",
                 strlen("MONITOR STOP\n")) < 0)
    {
        close(udp_sockfd);

        return -1;
    }

    if (receive_line(tcp_sockfd,
                     response,
                     sizeof(response)) < 0)
    {
        printf("Failed to receive MONITOR STOP response.\n");

        close(udp_sockfd);

        return -1;
    }

    printf("Agent: %s\n", response);

    if (strstr(response,
               "OK MONITOR_STOPPED") != NULL)
    {
        printf("UDP monitoring stopped successfully.\n");
    }

    close(udp_sockfd);

    return 0;
}


/* ============================================================
   Main
   ============================================================ */

int main(void)
{
    int sockfd;

    printf("========================================\n");
    printf("        RemoteOps Controller\n");
    printf("========================================\n");

    printf("SID: %s\n", SID);

    printf("Connecting to Agent %s:%d\n",
           SERVER_IP,
           SERVER_PORT);

    sockfd = connect_to_agent();

    if (sockfd < 0)
    {
        printf("Could not connect to Agent.\n");
        return 1;
    }

    printf("Connected to Agent successfully.\n");

    /*
     * AUTH must be the first command.
     */

    if (authenticate(sockfd) < 0)
    {
        close(sockfd);
        return 1;
    }

    /*
     * SYSINFO
     */

    send_command(sockfd, "SYSINFO");

    /*
     * LISTPROC
     */

    send_command(sockfd, "LISTPROC");

    /*
     * EXEC commands
     */

    send_command(sockfd, "EXEC DATE");

    send_command(sockfd, "EXEC UPTIME");

    send_command(sockfd, "EXEC DISKFREE");

    send_command(sockfd, "EXEC HOSTNAME");

    send_command(sockfd, "EXEC WHOAMI");

    /*
     * Test invalid EXEC command.
     */

    send_command(sockfd, "EXEC LS");

    /*
     * PUT
     */

    put_file(sockfd);

    /*
     * GET
     */

    get_file(sockfd);

    /*
     * UDP MONITORING
     */

    start_udp_monitor(sockfd);

    /*
     * Graceful disconnect
     */

    printf("\nSent: QUIT\n");

    if (send_all(sockfd,
                 "QUIT\n",
                 strlen("QUIT\n")) == 0)
    {
        char response[LINE_SIZE];

        if (receive_line(sockfd,
                         response,
                         sizeof(response)) == 0)
        {
            printf("Agent: %s\n", response);
        }
    }

    close(sockfd);

    printf("\nController finished successfully.\n");

    return 0;
}
