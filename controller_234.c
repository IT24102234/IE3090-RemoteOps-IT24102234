#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9410
#define AUTH_TOKEN "OPS-2234"

#define TEST_FILE "test.txt"
#define DOWNLOAD_FILE "downloaded_test.txt"

/*
 * Receive one line ending with '\n'.
 */
int receive_line(int socket_fd,
                 char *buffer,
                 int buffer_size)
{
    int total = 0;
    char ch;

    while (total < buffer_size - 1)
    {
        int n = recv(socket_fd,
                     &ch,
                     1,
                     0);

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

/*
 * Send exactly all requested bytes.
 */
int send_all(int socket_fd,
             const void *data,
             size_t total_bytes)
{
    const char *ptr = data;
    size_t total_sent = 0;

    while (total_sent < total_bytes)
    {
        ssize_t n = send(socket_fd,
                         ptr + total_sent,
                         total_bytes - total_sent,
                         0);

        if (n <= 0)
        {
            return -1;
        }

        total_sent += (size_t)n;
    }

    return 0;
}

/*
 * Receive exactly the requested number of bytes.
 */
int receive_all(int socket_fd,
                void *data,
                size_t total_bytes)
{
    char *ptr = data;
    size_t total_received = 0;

    while (total_received < total_bytes)
    {
        ssize_t n = recv(socket_fd,
                         ptr + total_received,
                         total_bytes - total_received,
                         0);

        if (n <= 0)
        {
            return -1;
        }

        total_received += (size_t)n;
    }

    return 0;
}

/*
 * Send an EXEC command and display the response.
 */
int send_exec_command(int sock,
                      char *buffer,
                      int buffer_size,
                      const char *command_name)
{
    char command[100];

    snprintf(command,
             sizeof(command),
             "EXEC %s\n",
             command_name);

    if (send_all(sock,
                 command,
                 strlen(command)) < 0)
    {
        return 0;
    }

    printf("Sent: EXEC %s\n",
           command_name);

    memset(buffer,
           0,
           buffer_size);

    int bytes_received =
        receive_line(sock,
                     buffer,
                     buffer_size);

    if (bytes_received <= 0)
    {
        printf("Failed to receive EXEC response\n");
        return 0;
    }

    printf("Agent: %s",
           buffer);

    return 1;
}

/*
 * PUT a local file to the Agent.
 */
int put_file(int sock,
             const char *filename)
{
    struct stat file_info;

    if (stat(filename,
             &file_info) != 0)
    {
        perror("stat");

        return 0;
    }

    unsigned long long filesize =
        (unsigned long long)file_info.st_size;

    printf("\n--- PUT TEST ---\n");

    printf("Local file: %s\n",
           filename);

    printf("File size: %llu bytes\n",
           filesize);

    /*
     * Send PUT header.
     */
    char header[512];

    snprintf(header,
             sizeof(header),
             "PUT %s %llu\n",
             filename,
             filesize);

    if (send_all(sock,
                 header,
                 strlen(header)) < 0)
    {
        printf("Failed to send PUT header\n");

        return 0;
    }

    printf("Sent: PUT %s %llu\n",
           filename,
           filesize);

    /*
     * Open local file.
     */
    FILE *file =
        fopen(filename, "rb");

    if (file == NULL)
    {
        perror("fopen");

        return 0;
    }

    /*
     * Send exactly the file bytes.
     */
    unsigned char data[4096];

    unsigned long long total_sent = 0;

    while (total_sent < filesize)
    {
        size_t remaining =
            (size_t)(filesize - total_sent);

        size_t chunk_size =
            remaining > sizeof(data)
            ? sizeof(data)
            : remaining;

        size_t bytes_read =
            fread(data,
                  1,
                  chunk_size,
                  file);

        if (bytes_read != chunk_size)
        {
            printf("Failed to read complete file\n");

            fclose(file);

            return 0;
        }

        if (send_all(sock,
                     data,
                     bytes_read) < 0)
        {
            printf("Failed to send file data\n");

            fclose(file);

            return 0;
        }

        total_sent +=
            (unsigned long long)bytes_read;
    }

    fclose(file);

    printf("Sent %llu raw file bytes\n",
           total_sent);

    /*
     * Receive Agent response.
     */
    char response[1024];

    int bytes_received =
        receive_line(sock,
                     response,
                     sizeof(response));

    if (bytes_received <= 0)
    {
        printf("Failed to receive PUT response\n");

        return 0;
    }

    printf("Agent: %s",
           response);

    return 1;
}

/*
 * GET a file from the Agent.
 */
int get_file(int sock,
             const char *filename,
             const char *output_filename)
{
    printf("\n--- GET TEST ---\n");

    /*
     * Send GET command.
     */
    char command[512];

    snprintf(command,
             sizeof(command),
             "GET %s\n",
             filename);

    if (send_all(sock,
                 command,
                 strlen(command)) < 0)
    {
        printf("Failed to send GET command\n");

        return 0;
    }

    printf("Sent: GET %s\n",
           filename);

    /*
     * Receive GET response header.
     */
    char response[1024];

    int bytes_received =
        receive_line(sock,
                     response,
                     sizeof(response));

    if (bytes_received <= 0)
    {
        printf("Failed to receive GET response\n");

        return 0;
    }

    printf("Agent: %s",
           response);

    /*
     * Check that the Agent returned
     * a successful file response.
     */
    if (strncmp(response,
                "OK FILE_SEND ",
                strlen("OK FILE_SEND ")) != 0)
    {
        printf("GET failed\n");

        return 0;
    }

    /*
     * Parse:
     *
     * OK FILE_SEND <filename> <filesize> SID:4322
     */
    char returned_filename[256];
    unsigned long long filesize;

    if (sscanf(response,
               "OK FILE_SEND %255s %llu",
               returned_filename,
               &filesize) != 2)
    {
        printf("Invalid GET response format\n");

        return 0;
    }

    printf("Expected file size: %llu bytes\n",
           filesize);

    /*
     * Open local output file.
     */
    FILE *file =
        fopen(output_filename, "wb");

    if (file == NULL)
    {
        perror("fopen output");

        return 0;
    }

    /*
     * Receive exactly the declared
     * number of raw bytes.
     */
    unsigned char data[4096];

    unsigned long long total_received = 0;

    while (total_received < filesize)
    {
        size_t remaining =
            (size_t)(filesize - total_received);

        size_t chunk_size =
            remaining > sizeof(data)
            ? sizeof(data)
            : remaining;

        if (receive_all(sock,
                        data,
                        chunk_size) < 0)
        {
            printf("Failed to receive complete file\n");

            fclose(file);

            return 0;
        }

        size_t bytes_written =
            fwrite(data,
                   1,
                   chunk_size,
                   file);

        if (bytes_written != chunk_size)
        {
            printf("Failed to write downloaded file\n");

            fclose(file);

            return 0;
        }

        total_received +=
            (unsigned long long)chunk_size;
    }

    fclose(file);

    printf("Received %llu raw file bytes\n",
           total_received);

    printf("Saved downloaded file as: %s\n",
           output_filename);

    return 1;
}

int main(void)
{
    int sock;

    struct sockaddr_in server_addr;

    char buffer[4096];

    /*
     * Create TCP socket.
     */
    sock =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (sock < 0)
    {
        perror("socket");

        return 1;
    }

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_port =
        htons(SERVER_PORT);

    /*
     * Convert server IP.
     */
    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");

        close(sock);

        return 1;
    }

    /*
     * Connect to Agent.
     */
    if (connect(sock,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");

        close(sock);

        return 1;
    }

    printf("Connected to RemoteOps Agent\n");

    /*
     * AUTH
     */
    char auth_message[100];

    snprintf(auth_message,
             sizeof(auth_message),
             "AUTH %s\n",
             AUTH_TOKEN);

    send_all(sock,
             auth_message,
             strlen(auth_message));

    printf("Sent: AUTH %s\n",
           AUTH_TOKEN);

    int bytes_received =
        receive_line(sock,
                     buffer,
                     sizeof(buffer));

    if (bytes_received <= 0)
    {
        printf("Failed to receive authentication response\n");

        close(sock);

        return 1;
    }

    printf("Agent: %s",
           buffer);

    /*
     * Check authentication.
     */
    if (strncmp(buffer,
                "OK AUTHENTICATED",
                strlen("OK AUTHENTICATED")) != 0)
    {
        printf("Authentication failed. Closing connection.\n");

        close(sock);

        return 1;
    }

    /*
     * SYSINFO
     */
    const char *sysinfo_command =
        "SYSINFO\n";

    send_all(sock,
             sysinfo_command,
             strlen(sysinfo_command));

    printf("Sent: SYSINFO\n");

    memset(buffer,
           0,
           sizeof(buffer));

    bytes_received =
        receive_line(sock,
                     buffer,
                     sizeof(buffer));

    if (bytes_received <= 0)
    {
        printf("Failed to receive SYSINFO response\n");

        close(sock);

        return 1;
    }

    printf("Agent: %s",
           buffer);

    /*
     * LISTPROC
     */
    const char *listproc_command =
        "LISTPROC\n";

    send_all(sock,
             listproc_command,
             strlen(listproc_command));

    printf("Sent: LISTPROC\n");

    memset(buffer,
           0,
           sizeof(buffer));

    bytes_received =
        receive_line(sock,
                     buffer,
                     sizeof(buffer));

    if (bytes_received <= 0)
    {
        printf("Failed to receive LISTPROC response\n");

        close(sock);

        return 1;
    }

    printf("Agent: %s",
           buffer);

    /*
     * EXEC DATE
     */
    if (!send_exec_command(sock,
                           buffer,
                           sizeof(buffer),
                           "DATE"))
    {
        close(sock);

        return 1;
    }

    /*
     * EXEC UPTIME
     */
    if (!send_exec_command(sock,
                           buffer,
                           sizeof(buffer),
                           "UPTIME"))
    {
        close(sock);

        return 1;
    }

    /*
     * EXEC DISKFREE
     */
    if (!send_exec_command(sock,
                           buffer,
                           sizeof(buffer),
                           "DISKFREE"))
    {
        close(sock);

        return 1;
    }

    /*
     * EXEC HOSTNAME
     */
    if (!send_exec_command(sock,
                           buffer,
                           sizeof(buffer),
                           "HOSTNAME"))
    {
        close(sock);

        return 1;
    }

    /*
     * EXEC WHOAMI
     */
    if (!send_exec_command(sock,
                           buffer,
                           sizeof(buffer),
                           "WHOAMI"))
    {
        close(sock);

        return 1;
    }

    /*
     * PUT test.txt
     */
    if (!put_file(sock,
                  TEST_FILE))
    {
        close(sock);

        return 1;
    }

    /*
     * GET test.txt
     */
    if (!get_file(sock,
                  TEST_FILE,
                  DOWNLOAD_FILE))
    {
        close(sock);

        return 1;
    }

    /*
     * QUIT
     */
    const char *quit_command =
        "QUIT\n";

    send_all(sock,
             quit_command,
             strlen(quit_command));

    printf("\nSent: QUIT\n");

    memset(buffer,
           0,
           sizeof(buffer));

    bytes_received =
        receive_line(sock,
                     buffer,
                     sizeof(buffer));

    if (bytes_received > 0)
    {
        printf("Agent: %s",
               buffer);
    }

    /*
     * Close connection.
     */
    close(sock);

    printf("Connection closed\n");

    return 0;
}
