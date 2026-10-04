#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#define PORT 9410
#define SID "4322"
#define AUTH_TOKEN "OPS-2234"

#define STORAGE_DIR "./agentfiles/IT24102234"
#define MAX_FILE_SIZE (10ULL * 1024ULL * 1024ULL)

#define MONITOR_INTERVAL 2

/*
 * Information belonging to one connected Controller.
 */
typedef struct
{
    int client_fd;
    struct sockaddr_in client_addr;

    int authenticated;

    int monitoring;
    int monitor_udp_port;

    int monitor_socket;
    pthread_t monitor_thread;

    pthread_mutex_t monitor_mutex;

} ClientSession;



/*
 * Per-connection TCP buffering.
 *
 * A single recv() may contain part of a line, several complete lines,
 * or a line followed immediately by raw PUT file bytes. Unread bytes
 * are kept in this buffer for the next operation.
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
 * Fill the receive buffer while preserving unread data.
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
 * Used for raw PUT file data after the PUT header.
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
 * Check whether a filename is safe.
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

    if (strcmp(command_name, "DATE") == 0)
    {
        system_command = "date";
    }
    else if (strcmp(command_name, "UPTIME") == 0)
    {
        system_command = "uptime";
    }
    else if (strcmp(command_name, "DISKFREE") == 0)
    {
        system_command = "df -h / | tail -n 1";
    }
    else if (strcmp(command_name, "HOSTNAME") == 0)
    {
        system_command = "hostname";
    }
    else if (strcmp(command_name, "WHOAMI") == 0)
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
        strcpy(output, "No output");
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
 */
int handle_put(BufferedSocket *stream,
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

        send_all(stream->fd,
                 response,
                 strlen(response));

        return -1;
    }

    if (!valid_filename(filename))
    {
        const char *response =
            "ERR 004 FILE_TOO_LARGE SID:4322\n";

        send_all(stream->fd,
                 response,
                 strlen(response));

        return -1;
    }

    if (filesize > MAX_FILE_SIZE)
    {
        const char *response =
            "ERR 004 FILE_TOO_LARGE SID:4322\n";

        send_all(stream->fd,
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
        const char *response =
            "ERR 003 FILE_WRITE_FAILED SID:4322\n";

        send_all(stream->fd,
                 response,
                 strlen(response));

        return -1;
    }

    unsigned char data[4096];

    unsigned long long remaining =
        filesize;

    while (remaining > 0)
    {
        size_t chunk_size =
            remaining > sizeof(data)
            ? sizeof(data)
            : (size_t)remaining;

        if (buffered_read_exact(stream,
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

            send_all(stream->fd,
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

    send_all(stream->fd,
             response,
             strlen(response));

    printf("PUT completed: %s (%llu bytes)\n",
           filename,
           filesize);

    return 0;
}


/*
 * Handle GET.
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

    if (fseek(file, 0, SEEK_END) != 0)
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


/*
 * UDP monitoring thread.
 *
 * Sends SYSINFO-style UDP datagrams every
 * MONITOR_INTERVAL seconds.
 */
void *monitor_worker(void *arg)
{
    ClientSession *session =
        (ClientSession *)arg;

    while (1)
    {
        sleep(MONITOR_INTERVAL);

        pthread_mutex_lock(
            &session->monitor_mutex);

        int active =
            session->monitoring;

        int udp_socket =
            session->monitor_socket;

        struct sockaddr_in destination;

        memset(&destination,
               0,
               sizeof(destination));

        destination.sin_family =
            AF_INET;

        destination.sin_addr =
            session->client_addr.sin_addr;

        destination.sin_port =
            htons(session->monitor_udp_port);

        pthread_mutex_unlock(
            &session->monitor_mutex);

        if (!active)
        {
            break;
        }

        double cpu_load;
        long mem_used_mb;
        long uptime_sec;

        get_sysinfo(&cpu_load,
                    &mem_used_mb,
                    &uptime_sec);

        char message[256];

        snprintf(message,
                 sizeof(message),
                 "SYSINFO %.2f %ld %ld SID:4322",
                 cpu_load,
                 mem_used_mb,
                 uptime_sec);

        sendto(udp_socket,
               message,
               strlen(message),
               0,
               (struct sockaddr *)&destination,
               sizeof(destination));

        printf("UDP monitoring sent: %s\n",
               message);
    }

    return NULL;
}


/*
 * Start UDP monitoring.
 */
int start_monitoring(ClientSession *session,
                     int udp_port)
{
    pthread_mutex_lock(
        &session->monitor_mutex);

    if (session->monitoring)
    {
        pthread_mutex_unlock(
            &session->monitor_mutex);

        return 0;
    }

    int udp_socket =
        socket(AF_INET,
               SOCK_DGRAM,
               0);

    if (udp_socket < 0)
    {
        pthread_mutex_unlock(
            &session->monitor_mutex);

        perror("UDP socket");

        return -1;
    }

    session->monitor_socket =
        udp_socket;

    session->monitor_udp_port =
        udp_port;

    session->monitoring =
        1;

    pthread_mutex_unlock(
        &session->monitor_mutex);

    if (pthread_create(&session->monitor_thread,
                       NULL,
                       monitor_worker,
                       session) != 0)
    {
        pthread_mutex_lock(
            &session->monitor_mutex);

        session->monitoring = 0;

        close(session->monitor_socket);

        pthread_mutex_unlock(
            &session->monitor_mutex);

        perror("pthread_create");

        return -1;
    }

    printf("UDP monitoring started on port %d\n",
           udp_port);

    return 1;
}


/*
 * Stop UDP monitoring.
 */
void stop_monitoring(ClientSession *session)
{
    pthread_mutex_lock(
        &session->monitor_mutex);

    int was_active =
        session->monitoring;

    session->monitoring =
        0;

    pthread_mutex_unlock(
        &session->monitor_mutex);

    if (was_active)
    {
        pthread_join(
            session->monitor_thread,
            NULL);

        pthread_mutex_lock(
            &session->monitor_mutex);

        close(session->monitor_socket);

        session->monitor_socket = -1;

        pthread_mutex_unlock(
            &session->monitor_mutex);

        printf("UDP monitoring stopped\n");
    }
}


/*
 * Handle one Controller connection.
 *
 * Each Controller receives its own thread.
 */
void *controller_worker(void *arg)
{
    ClientSession *session =
        (ClientSession *)arg;

    int client_fd =
        session->client_fd;

    BufferedSocket stream =
    {
        .fd = client_fd,
        .start = 0,
        .end = 0
    };

    printf("Controller connected from %s:%d\n",
           inet_ntoa(session->client_addr.sin_addr),
           ntohs(session->client_addr.sin_port));

    char buffer[4096];

    /*
     * First command MUST be AUTH.
     */
    int bytes_received =
        buffered_read_line(&stream,
                     buffer,
                     sizeof(buffer));

    if (bytes_received <= 0)
    {
        printf("Controller disconnected before authentication\n");

        close(client_fd);

        pthread_mutex_destroy(
            &session->monitor_mutex);

        free(session);

        return NULL;
    }

    char expected_auth[100];

    snprintf(expected_auth,
             sizeof(expected_auth),
             "AUTH %s\n",
             AUTH_TOKEN);

    if (strcmp(buffer,
               expected_auth) != 0)
    {
        const char *response =
            "ERR 001 AUTH_FAILED SID:4322\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        printf("Controller authentication failed\n");

        close(client_fd);

        pthread_mutex_destroy(
            &session->monitor_mutex);

        free(session);

        return NULL;
    }

    const char *auth_response =
        "OK AUTHENTICATED SID:4322\n";

    send_all(client_fd,
             auth_response,
             strlen(auth_response));

    session->authenticated =
        1;

    printf("Controller authenticated successfully\n");


    /*
     * Process commands.
     */
    while (1)
    {
        bytes_received =
            buffered_read_line(&stream,
                         buffer,
                         sizeof(buffer));

        if (bytes_received <= 0)
        {
            printf("Controller disconnected unexpectedly\n");

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
            handle_put(&stream,
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
         * MONITOR START
         *
         * Format:
         * MONITOR START <udp_port>
         */
        else if (strncmp(buffer,
                         "MONITOR START ",
                         14) == 0)
        {
            int udp_port;

            if (sscanf(buffer + 14,
                       "%d",
                       &udp_port) != 1 ||
                udp_port < 1 ||
                udp_port > 65535)
            {
                const char *response =
                    "ERR 003 UNKNOWN_COMMAND SID:4322\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                continue;
            }

            int result =
                start_monitoring(session,
                                 udp_port);

            if (result > 0)
            {
                const char *response =
                    "OK MONITOR_STARTED SID:4322\n";

                send_all(client_fd,
                         response,
                         strlen(response));
            }
            else
            {
                const char *response =
                    "ERR 003 MONITOR_FAILED SID:4322\n";

                send_all(client_fd,
                         response,
                         strlen(response));
            }
        }


        /*
         * MONITOR STOP
         */
        else if (strcmp(buffer,
                        "MONITOR STOP\n") == 0)
        {
            stop_monitoring(session);

            const char *response =
                "OK MONITOR_STOPPED SID:4322\n";

            send_all(client_fd,
                     response,
                     strlen(response));
        }


        /*
         * QUIT
         */
        else if (strcmp(buffer,
                        "QUIT\n") == 0)
        {
            stop_monitoring(session);

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


    /*
     * Make sure monitoring is stopped
     * if the Controller disconnects.
     */
    stop_monitoring(session);

    close(client_fd);

    printf("Controller connection closed\n");

    pthread_mutex_destroy(
        &session->monitor_mutex);

    free(session);

    return NULL;
}


int main(void)
{
    int server_fd;

    struct sockaddr_in server_addr;

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
    if (listen(server_fd, 10) < 0)
    {
        perror("listen");

        close(server_fd);

        return 1;
    }


    printf("RemoteOps Agent started\n");
    printf("SID: %s\n", SID);
    printf("Listening on TCP port %d\n", PORT);
    printf("Storage: %s\n", STORAGE_DIR);
    printf("Concurrency: pthreads\n");
    printf("UDP monitoring interval: %d seconds\n",
           MONITOR_INTERVAL);


    /*
     * Accept Controllers.
     */
    while (1)
    {
        struct sockaddr_in client_addr;

        socklen_t client_len =
            sizeof(client_addr);

        int client_fd =
            accept(server_fd,
                   (struct sockaddr *)&client_addr,
                   &client_len);

        if (client_fd < 0)
        {
            perror("accept");

            continue;
        }


        /*
         * Create session structure.
         */
        ClientSession *session =
            malloc(sizeof(ClientSession));

        if (session == NULL)
        {
            perror("malloc");

            close(client_fd);

            continue;
        }

        memset(session,
               0,
               sizeof(ClientSession));

        session->client_fd =
            client_fd;

        session->client_addr =
            client_addr;

        session->authenticated =
            0;

        session->monitoring =
            0;

        session->monitor_socket =
            -1;

        pthread_mutex_init(
            &session->monitor_mutex,
            NULL);


        /*
         * Create Controller thread.
         */
        pthread_t controller_thread;

        if (pthread_create(
                &controller_thread,
                NULL,
                controller_worker,
                session) != 0)
        {
            perror("pthread_create");

            pthread_mutex_destroy(
                &session->monitor_mutex);

            free(session);

            close(client_fd);

            continue;
        }


        /*
         * We do not need to join Controller
         * threads because they clean themselves up.
         */
        pthread_detach(
            controller_thread);
    }


    close(server_fd);

    return 0;
}
