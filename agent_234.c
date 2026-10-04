#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define PORT 9410
#define SID "4322"
#define AUTH_TOKEN "OPS-2234"

#define STORAGE_DIR "./agentfiles/IT24102234"
#define MAX_FILE_SIZE (10ULL * 1024ULL * 1024ULL)

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
 * Check whether a filename is safe.
 *
 * Only a simple filename is allowed.
 * Paths such as ../file.txt or dir/file.txt
 * are rejected.
 */
int valid_filename(const char *filename)
{
    if (filename == NULL ||
        strlen(filename) == 0)
    {
        return 0;
    }

    if (strstr(filename, "..") != NULL)
    {
        return 0;
    }

    if (strchr(filename, '/') != NULL)
    {
        return 0;
    }

    if (strchr(filename, '\\') != NULL)
    {
        return 0;
    }

    return 1;
}

/*
 * Get current system information.
 */
void get_sysinfo(double *cpu_load,
                 long *mem_used_mb,
                 long *uptime_sec)
{
    FILE *file;
    char line[256];

    /*
     * CPU load.
     */
    file = fopen("/proc/loadavg", "r");

    if (file != NULL)
    {
        fscanf(file, "%lf", cpu_load);
        fclose(file);
    }
    else
    {
        *cpu_load = 0.0;
    }

    /*
     * Memory usage.
     */
    long mem_total = 0;
    long mem_available = 0;

    file = fopen("/proc/meminfo", "r");

    if (file != NULL)
    {
        while (fgets(line,
                     sizeof(line),
                     file) != NULL)
        {
            if (sscanf(line,
                       "MemTotal: %ld kB",
                       &mem_total) == 1)
            {
                continue;
            }

            if (sscanf(line,
                       "MemAvailable: %ld kB",
                       &mem_available) == 1)
            {
                continue;
            }
        }

        fclose(file);
    }

    long mem_used =
        mem_total - mem_available;

    *mem_used_mb =
        mem_used / 1024;

    /*
     * Uptime.
     */
    double uptime;

    file = fopen("/proc/uptime", "r");

    if (file != NULL)
    {
        fscanf(file,
               "%lf",
               &uptime);

        fclose(file);

        *uptime_sec =
            (long)uptime;
    }
    else
    {
        *uptime_sec = 0;
    }
}

/*
 * Get a snapshot of running processes.
 */
void get_process_list(char *output,
                      int output_size)
{
    FILE *file;
    char line[256];
    int first = 1;

    output[0] = '\0';

    file = popen("ps -e -o pid=,comm= | head -n 10",
                 "r");

    if (file == NULL)
    {
        snprintf(output,
                 output_size,
                 "Unable_to_retrieve_processes");

        return;
    }

    while (fgets(line,
                 sizeof(line),
                 file) != NULL)
    {
        int pid;
        char process_name[128];

        if (sscanf(line,
                   "%d %127s",
                   &pid,
                   process_name) == 2)
        {
            char entry[160];

            snprintf(entry,
                     sizeof(entry),
                     "%d:%s",
                     pid,
                     process_name);

            if (!first)
            {
                strncat(output,
                        ",",
                        output_size -
                        strlen(output) -
                        1);
            }

            strncat(output,
                    entry,
                    output_size -
                    strlen(output) -
                    1);

            first = 0;
        }
    }

    pclose(file);
}

/*
 * Execute one of the five allowed commands.
 */
void execute_command(int client_fd,
                     const char *command_name)
{
    const char *system_command = NULL;

    if (strcmp(command_name,
               "DATE") == 0)
    {
        system_command = "date";
    }
    else if (strcmp(command_name,
                    "UPTIME") == 0)
    {
        system_command = "uptime";
    }
    else if (strcmp(command_name,
                    "DISKFREE") == 0)
    {
        system_command =
            "df -h / | tail -n 1";
    }
    else if (strcmp(command_name,
                    "HOSTNAME") == 0)
    {
        system_command = "hostname";
    }
    else if (strcmp(command_name,
                    "WHOAMI") == 0)
    {
        system_command = "whoami";
    }
    else
    {
        const char *response =
            "ERR 002 COMMAND_NOT_ALLOWED SID:4322\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        printf("EXEC command rejected: %s\n",
               command_name);

        return;
    }

    FILE *command_file =
        popen(system_command, "r");

    if (command_file == NULL)
    {
        const char *response =
            "ERR 003 EXEC_FAILED SID:4322\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        return;
    }

    char output[1024];

    if (fgets(output,
              sizeof(output),
              command_file) != NULL)
    {
        output[strcspn(output,
                       "\r\n")] = '\0';
    }
    else
    {
        strcpy(output,
               "No output");
    }

    pclose(command_file);

    char response[1200];

    snprintf(response,
             sizeof(response),
             "OK EXEC_RESULT %s SID:4322\n",
             output);

    send_all(client_fd,
             response,
             strlen(response));

    printf("EXEC %s processed\n",
           command_name);
}

/*
 * Handle PUT.
 *
 * Format:
 * PUT <filename> <filesize>\n
 * followed immediately by exactly <filesize>
 * raw bytes.
 */
int handle_put(int client_fd,
               char *buffer)
{
    char filename[256];
    unsigned long long filesize;

    if (sscanf(buffer,
               "PUT %255s %llu",
               filename,
               &filesize) != 2)
    {
        const char *response =
            "ERR 004 FILE_TOO_LARGE SID:4322\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        return -1;
    }

    if (!valid_filename(filename))
    {
        const char *response =
            "ERR 004 FILE_TOO_LARGE SID:4322\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        return -1;
    }

    if (filesize > MAX_FILE_SIZE)
    {
        const char *response =
            "ERR 004 FILE_TOO_LARGE SID:4322\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        printf("PUT rejected because file is too large: %s\n",
               filename);

        return -1;
    }

    char filepath[512];

    snprintf(filepath,
             sizeof(filepath),
             "%s/%s",
             STORAGE_DIR,
             filename);

    FILE *file =
        fopen(filepath, "wb");

    if (file == NULL)
    {
        perror("fopen PUT");

        const char *response =
            "ERR 003 FILE_WRITE_FAILED SID:4322\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        return -1;
    }

    /*
     * Receive the exact file size.
     */
    unsigned char data[4096];
    unsigned long long remaining =
        filesize;

    while (remaining > 0)
    {
        size_t chunk_size =
            remaining > sizeof(data)
            ? sizeof(data)
            : (size_t)remaining;

        if (receive_all(client_fd,
                         data,
                         chunk_size) < 0)
        {
            fclose(file);

            printf("PUT interrupted: %s\n",
                   filename);

            return -1;
        }

        size_t written =
            fwrite(data,
                   1,
                   chunk_size,
                   file);

        if (written != chunk_size)
        {
            fclose(file);

            const char *response =
                "ERR 003 FILE_WRITE_FAILED SID:4322\n";

            send_all(client_fd,
                     response,
                     strlen(response));

            return -1;
        }

        remaining -= chunk_size;
    }

    fclose(file);

    char response[512];

    snprintf(response,
             sizeof(response),
             "OK FILE_RECEIVED %s SID:4322\n",
             filename);

    send_all(client_fd,
             response,
             strlen(response));

    printf("PUT completed: %s (%llu bytes)\n",
           filename,
           filesize);

    return 0;
}

/*
 * Handle GET.
 *
 * Format:
 * GET <filename>\n
 *
 * Response:
 * OK FILE_SEND <filename> <filesize> SID:4322\n
 *
 * followed immediately by exactly <filesize>
 * raw bytes.
 */
int handle_get(int client_fd,
               char *buffer)
{
    char filename[256];

    if (sscanf(buffer,
               "GET %255s",
               filename) != 1)
    {
        const char *response =
            "ERR 005 FILE_NOT_FOUND SID:4322\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        return -1;
    }

    filename[
        strcspn(filename, "\r\n")
    ] = '\0';

    if (!valid_filename(filename))
    {
        const char *response =
            "ERR 005 FILE_NOT_FOUND SID:4322\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        return -1;
    }

    char filepath[512];

    snprintf(filepath,
             sizeof(filepath),
             "%s/%s",
             STORAGE_DIR,
             filename);

    FILE *file =
        fopen(filepath, "rb");

    if (file == NULL)
    {
        const char *response =
            "ERR 005 FILE_NOT_FOUND SID:4322\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        printf("GET file not found: %s\n",
               filename);

        return -1;
    }

    /*
     * Determine file size.
     */
    if (fseek(file,
              0,
              SEEK_END) != 0)
    {
        fclose(file);
        return -1;
    }

    long file_size =
        ftell(file);

    if (file_size < 0)
    {
        fclose(file);
        return -1;
    }

    rewind(file);

    /*
     * Send GET response header.
     */
    char response[512];

    snprintf(response,
             sizeof(response),
             "OK FILE_SEND %s %ld SID:4322\n",
             filename,
             file_size);

    if (send_all(client_fd,
                 response,
                 strlen(response)) < 0)
    {
        fclose(file);
        return -1;
    }

    /*
     * Send exactly the file bytes.
     */
    unsigned char data[4096];

    long remaining =
        file_size;

    while (remaining > 0)
    {
        size_t chunk_size =
            remaining > (long)sizeof(data)
            ? sizeof(data)
            : (size_t)remaining;

        size_t bytes_read =
            fread(data,
                  1,
                  chunk_size,
                  file);

        if (bytes_read != chunk_size)
        {
            fclose(file);
            return -1;
        }

        if (send_all(client_fd,
                     data,
                     bytes_read) < 0)
        {
            fclose(file);
            return -1;
        }

        remaining -=
            (long)bytes_read;
    }

    fclose(file);

    printf("GET completed: %s (%ld bytes)\n",
           filename,
           file_size);

    return 0;
}

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len =
        sizeof(client_addr);

    /*
     * Create personalised storage directory.
     */
    if (mkdir("./agentfiles", 0755) < 0 &&
        errno != EEXIST)
    {
        perror("mkdir agentfiles");
    }

    if (mkdir(STORAGE_DIR, 0755) < 0 &&
        errno != EEXIST)
    {
        perror("mkdir storage");
    }

    /*
     * Create TCP socket.
     */
    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    /*
     * Allow port reuse.
     */
    int opt = 1;

    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt)) < 0)
    {
        perror("setsockopt");

        close(server_fd);

        return 1;
    }

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);

    /*
     * Bind.
     */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");

        close(server_fd);

        return 1;
    }

    /*
     * Listen.
     */
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");

        close(server_fd);

        return 1;
    }

    printf("RemoteOps Agent started\n");
    printf("SID: %s\n", SID);
    printf("Listening on TCP port %d\n", PORT);
    printf("Storage: %s\n", STORAGE_DIR);

    /*
     * Accept Controllers.
     */
    while (1)
    {
        client_fd =
            accept(server_fd,
                   (struct sockaddr *)&client_addr,
                   &client_len);

        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        printf("Controller connected\n");

        char buffer[4096];

        /*
         * First command MUST be AUTH.
         */
        int bytes_received =
            receive_line(client_fd,
                         buffer,
                         sizeof(buffer));

        if (bytes_received <= 0)
        {
            printf("Controller disconnected before authentication\n");

            close(client_fd);

            continue;
        }

        /*
         * Expected AUTH message.
         */
        char expected_auth[100];

        snprintf(expected_auth,
                 sizeof(expected_auth),
                 "AUTH %s\n",
                 AUTH_TOKEN);

        /*
         * Authenticate.
         */
        if (strcmp(buffer,
                   expected_auth) == 0)
        {
            const char *response =
                "OK AUTHENTICATED SID:4322\n";

            send_all(client_fd,
                     response,
                     strlen(response));

            printf("Controller authenticated successfully\n");

            /*
             * Process commands.
             */
            while (1)
            {
                bytes_received =
                    receive_line(client_fd,
                                 buffer,
                                 sizeof(buffer));

                if (bytes_received <= 0)
                {
                    printf("Controller disconnected\n");
                    break;
                }

                /*
                 * SYSINFO
                 */
                if (strcmp(buffer,
                           "SYSINFO\n") == 0)
                {
                    double cpu_load;
                    long mem_used_mb;
                    long uptime_sec;

                    get_sysinfo(&cpu_load,
                                &mem_used_mb,
                                &uptime_sec);

                    char response[256];

                    snprintf(response,
                             sizeof(response),
                             "OK SYSINFO %.2f %ld %ld SID:4322\n",
                             cpu_load,
                             mem_used_mb,
                             uptime_sec);

                    send_all(client_fd,
                             response,
                             strlen(response));

                    printf("SYSINFO command processed\n");
                }

                /*
                 * LISTPROC
                 */
                else if (strcmp(buffer,
                                "LISTPROC\n") == 0)
                {
                    char process_list[2048];
                    char response[2200];

                    get_process_list(process_list,
                                     sizeof(process_list));

                    snprintf(response,
                             sizeof(response),
                             "OK PROCS %s SID:4322\n",
                             process_list);

                    send_all(client_fd,
                             response,
                             strlen(response));

                    printf("LISTPROC command processed\n");
                }

                /*
                 * EXEC
                 */
                else if (strncmp(buffer,
                                 "EXEC ",
                                 5) == 0)
                {
                    char command_name[64];

                    if (sscanf(buffer,
                               "EXEC %63s",
                               command_name) != 1)
                    {
                        const char *response =
                            "ERR 002 COMMAND_NOT_ALLOWED SID:4322\n";

                        send_all(client_fd,
                                 response,
                                 strlen(response));

                        continue;
                    }

                    command_name[
                        strcspn(command_name,
                                "\r\n")
                    ] = '\0';

                    execute_command(client_fd,
                                    command_name);
                }

                /*
                 * PUT
                 */
                else if (strncmp(buffer,
                                 "PUT ",
                                 4) == 0)
                {
                    handle_put(client_fd,
                               buffer);
                }

                /*
                 * GET
                 */
                else if (strncmp(buffer,
                                 "GET ",
                                 4) == 0)
                {
                    handle_get(client_fd,
                               buffer);
                }

                /*
                 * QUIT
                 */
                else if (strcmp(buffer,
                                "QUIT\n") == 0)
                {
                    const char *response =
                        "OK BYE SID:4322\n";

                    send_all(client_fd,
                             response,
                             strlen(response));

                    printf("Controller requested disconnect\n");

                    break;
                }

                /*
                 * Unknown command.
                 */
                else
                {
                    const char *response =
                        "ERR 003 UNKNOWN_COMMAND SID:4322\n";

                    send_all(client_fd,
                             response,
                             strlen(response));
                }
            }
        }

        /*
         * Authentication failed.
         */
        else
        {
            const char *response =
                "ERR 001 AUTH_FAILED SID:4322\n";

            send_all(client_fd,
                     response,
                     strlen(response));

            printf("Controller authentication failed\n");
        }

        close(client_fd);
    }

    close(server_fd);

    return 0;
}
