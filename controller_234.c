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
#include <netinet/in.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9410

#define AUTH_TOKEN "OPS-2234"
#define SID "4322"

#define TEST_FILE "test.txt"
#define DOWNLOAD_FILE "downloaded_test.txt"

#define BUFFER_SIZE 4096
#define LINE_SIZE 1024

/* UDP port can be changed for each Controller */
int udp_port = 9500;


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



/*
 * Per-connection TCP buffering.
 *
 * TCP is a byte stream, so one recv() can contain a partial line,
 * multiple lines, or a line followed by raw GET file bytes.
 * Unread bytes remain in this buffer for the next operation.
 */
#define TCP_BUFFER_SIZE 8192

typedef struct
{
    int fd;
    unsigned char data[TCP_BUFFER_SIZE];
    size_t start;
    size_t end;
} BufferedSocket;


/*
 * Fill the receive buffer while preserving unread bytes.
 */
static int buffered_fill(BufferedSocket *stream)
{
    if (stream->start > 0)
    {
        if (stream->start < stream->end)
        {
            memmove(stream->data,
                    stream->data + stream->start,
                    stream->end - stream->start);
        }

        stream->end -= stream->start;
        stream->start = 0;
    }

    if (stream->end == TCP_BUFFER_SIZE)
    {
        return -2;
    }

    ssize_t received = recv(stream->fd,
                            stream->data + stream->end,
                            TCP_BUFFER_SIZE - stream->end,
                            0);

    if (received <= 0)
    {
        return (int)received;
    }

    stream->end += (size_t)received;
    return (int)received;
}


/*
 * Read one newline-terminated protocol line.
 * Bytes after the newline remain buffered.
 */
static int buffered_read_line(BufferedSocket *stream,
                               char *output,
                               size_t output_size)
{
    while (1)
    {
        for (size_t i = stream->start; i < stream->end; i++)
        {
            if (stream->data[i] == '\n')
            {
                size_t line_length = i - stream->start + 1;

                if (line_length >= output_size)
                {
                    return -2;
                }

                memcpy(output,
                       stream->data + stream->start,
                       line_length);

                output[line_length] = '\0';
                stream->start = i + 1;

                return (int)line_length;
            }
        }

        if (stream->end - stream->start >= output_size - 1)
        {
            return -2;
        }

        int result = buffered_fill(stream);

        if (result <= 0)
        {
            return result;
        }
    }
}


/*
 * Read exactly the requested number of bytes.
 * Used for the raw GET payload after its response header.
 */
static int buffered_read_exact(BufferedSocket *stream,
                                void *output,
                                size_t total_bytes)
{
    unsigned char *destination = output;
    size_t total_read = 0;

    while (total_read < total_bytes)
    {
        size_t available = stream->end - stream->start;

        if (available > 0)
        {
            size_t needed = total_bytes - total_read;
            size_t copy_size =
                available < needed ? available : needed;

            memcpy(destination + total_read,
                   stream->data + stream->start,
                   copy_size);

            stream->start += copy_size;
            total_read += copy_size;
            continue;
        }

        ssize_t received = recv(stream->fd,
                                destination + total_read,
                                total_bytes - total_read,
                                0);

        if (received <= 0)
        {
            return -1;
        }

        total_read += (size_t)received;
    }

    return 0;
}


/* ============================================================
   Connect to Agent
   ============================================================ */

int connect_to_agent(void)
{
    int sockfd;

    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET,
                    SOCK_STREAM,
                    0);

    if (sockfd < 0)
    {
        perror("socket");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0)
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

int authenticate(BufferedSocket *stream)
{
    char response[LINE_SIZE];

    char request[LINE_SIZE];

    printf("\n--- AUTHENTICATION ---\n");

    printf("Sending authentication token: %s\n",
           AUTH_TOKEN);

    snprintf(request,
             sizeof(request),
             "AUTH %s\n",
             AUTH_TOKEN);

    if (send_all(stream->fd,
                 request,
                 strlen(request)) < 0)
    {
        perror("send");

        return -1;
    }

    if (buffered_read_line(stream,
                     response,
                     sizeof(response)) < 0)
    {
        printf("Failed to receive authentication response.\n");

        return -1;
    }

    printf("Agent: %s\n", response);

    if (strstr(response,
               "OK AUTHENTICATED") != NULL)
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

int send_command(BufferedSocket *stream,
                  const char *command)
{
    char request[LINE_SIZE];

    char response[BUFFER_SIZE];

    snprintf(request,
             sizeof(request),
             "%s\n",
             command);

    printf("\nSent: %s\n",
           command);

    if (send_all(stream->fd,
                 request,
                 strlen(request)) < 0)
    {
        perror("send");

        return -1;
    }

    if (buffered_read_line(stream,
                     response,
                     sizeof(response)) < 0)
    {
        printf("Failed to receive response.\n");

        return -1;
    }

    printf("Agent: %s\n",
           response);

    return 0;
}


/* ============================================================
   PUT
   ============================================================ */

int put_file(BufferedSocket *stream)
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

    if (fread(data,
              1,
              filesize,
              file) != (size_t)filesize)
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

    if (send_all(stream->fd,
                 request,
                 strlen(request)) < 0)
    {
        free(data);

        return -1;
    }

    if (send_all(stream->fd,
                 data,
                 filesize) < 0)
    {
        free(data);

        return -1;
    }

    printf("Sent %ld raw file bytes\n",
           filesize);

    free(data);

    if (buffered_read_line(stream,
                     response,
                     sizeof(response)) < 0)
    {
        printf("Failed to receive PUT response.\n");

        return -1;
    }

    printf("Agent: %s\n",
           response);

    return 0;
}


/* ============================================================
   GET
   ============================================================ */

int get_file(BufferedSocket *stream)
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

    printf("\nSent: GET %s\n",
           TEST_FILE);

    if (send_all(stream->fd,
                 request,
                 strlen(request)) < 0)
    {
        return -1;
    }

    if (buffered_read_line(stream,
                     response,
                     sizeof(response)) < 0)
    {
        printf("Failed to receive GET response.\n");

        return -1;
    }

    printf("Agent: %s\n",
           response);

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

    if (buffered_read_exact(stream,
                    data,
                    filesize) < 0)
    {
        printf("Failed to receive file data.\n");

        free(data);

        return -1;
    }

    printf("Received %ld raw file bytes\n",
           filesize);

    FILE *file = fopen(DOWNLOAD_FILE, "wb");

    if (file == NULL)
    {
        perror("fopen downloaded file");

        free(data);

        return -1;
    }

    fwrite(data,
           1,
           filesize,
           file);

    fclose(file);

    free(data);

    printf("Saved downloaded file as %s\n",
           DOWNLOAD_FILE);

    return 0;
}


/* ============================================================
   UDP Monitoring
   ============================================================ */

int start_udp_monitor(BufferedSocket *stream)
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

    udp_addr.sin_port = htons(udp_port);

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
           udp_port);

    snprintf(request,
             sizeof(request),
             "MONITOR START %d\n",
             udp_port);

    printf("Sent: MONITOR START %d\n",
           udp_port);

    if (send_all(stream->fd,
                 request,
                 strlen(request)) < 0)
    {
        close(udp_sockfd);

        return -1;
    }

    if (buffered_read_line(stream,
                     response,
                     sizeof(response)) < 0)
    {
        printf("Failed to receive MONITOR START response.\n");

        close(udp_sockfd);

        return -1;
    }

    printf("Agent: %s\n",
           response);

    if (strstr(response,
               "OK MONITOR_STARTED") == NULL)
    {
        printf("Monitoring did not start.\n");

        close(udp_sockfd);

        return -1;
    }

    printf("\nWaiting for UDP monitoring packets...\n");

    /*
     * Receive approximately three monitoring packets.
     * The Agent sends one packet every two seconds.
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

    if (send_all(stream->fd,
                 "MONITOR STOP\n",
                 strlen("MONITOR STOP\n")) < 0)
    {
        close(udp_sockfd);

        return -1;
    }

    if (buffered_read_line(stream,
                     response,
                     sizeof(response)) < 0)
    {
        printf("Failed to receive MONITOR STOP response.\n");

        close(udp_sockfd);

        return -1;
    }

    printf("Agent: %s\n",
           response);

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

int main(int argc, char *argv[])
{
    int sockfd;

    /*
     * Optional UDP port argument.
     *
     * Example:
     * ./controller_234 9501
     */

    if (argc > 1)
    {
        udp_port = atoi(argv[1]);

        if (udp_port < 1024 ||
            udp_port > 65535)
        {
            printf("Invalid UDP port.\n");

            return 1;
        }
    }

    printf("========================================\n");
    printf("        RemoteOps Controller\n");
    printf("========================================\n");

    printf("SID: %s\n",
           SID);

    printf("Connecting to Agent %s:%d\n",
           SERVER_IP,
           SERVER_PORT);

    printf("Controller UDP port: %d\n",
           udp_port);

    sockfd = connect_to_agent();

    if (sockfd < 0)
    {
        printf("Could not connect to Agent.\n");

        return 1;
    }

    printf("Connected to Agent successfully.\n");

    BufferedSocket stream =
    {
        .fd = sockfd,
        .start = 0,
        .end = 0
    };

    /*
     * AUTH must be the first command.
     */

    if (authenticate(&stream) < 0)
    {
        close(sockfd);

        return 1;
    }

    /*
     * SYSINFO
     */

    send_command(&stream,
                 "SYSINFO");

    /*
     * LISTPROC
     */

    send_command(&stream,
                 "LISTPROC");

    /*
     * EXEC commands
     */

    send_command(&stream,
                 "EXEC DATE");

    send_command(&stream,
                 "EXEC UPTIME");

    send_command(&stream,
                 "EXEC DISKFREE");

    send_command(&stream,
                 "EXEC HOSTNAME");

    send_command(&stream,
                 "EXEC WHOAMI");

    /*
     * Test invalid EXEC command.
     */

    send_command(&stream,
                 "EXEC LS");

    /*
     * PUT
     */

    put_file(&stream);

    /*
     * GET
     */

    get_file(&stream);

    /*
     * UDP MONITORING
     */

    start_udp_monitor(&stream);

    /*
     * Graceful disconnect
     */

    printf("\nSent: QUIT\n");

    if (send_all(stream.fd,
                 "QUIT\n",
                 strlen("QUIT\n")) == 0)
    {
        char response[LINE_SIZE];

        if (buffered_read_line(&stream,
                         response,
                         sizeof(response)) == 0)
        {
            printf("Agent: %s\n",
                   response);
        }
    }

    close(sockfd);

    printf("\nController finished successfully.\n");

    return 0;
}
