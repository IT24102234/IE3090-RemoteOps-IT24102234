#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define SID "4322"
#define AUTH_TOKEN "OPS-2234"

/*
 * Receive one line ending with '\n'.
 */
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

/*
 * Get current system information.
 */
void get_sysinfo(double *cpu_load, long *mem_used_mb, long *uptime_sec)
{
    FILE *file;
    char line[256];

    /*
     * CPU load from /proc/loadavg
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
     * Memory usage from /proc/meminfo
     */
    long mem_total = 0;
    long mem_available = 0;

    file = fopen("/proc/meminfo", "r");

    if (file != NULL)
    {
        while (fgets(line, sizeof(line), file) != NULL)
        {
            if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1)
            {
                continue;
            }

            if (sscanf(line, "MemAvailable: %ld kB",
                       &mem_available) == 1)
            {
                continue;
            }
        }

        fclose(file);
    }

    long mem_used = mem_total - mem_available;

    *mem_used_mb = mem_used / 1024;

    /*
     * Uptime from /proc/uptime
     */
    double uptime;

    file = fopen("/proc/uptime", "r");

    if (file != NULL)
    {
        fscanf(file, "%lf", &uptime);
        fclose(file);

        *uptime_sec = (long)uptime;
    }
    else
    {
        *uptime_sec = 0;
    }
}

/*
 * Get a snapshot of currently running processes.
 *
 * The assignment allows a simple ps-based snapshot.
 * We return up to 10 processes as PID:process_name pairs.
 */
void get_process_list(char *output, int output_size)
{
    FILE *file;
    char line[256];
    int first = 1;

    output[0] = '\0';

    file = popen("ps -e -o pid=,comm= | head -n 10", "r");

    if (file == NULL)
    {
        snprintf(output,
                 output_size,
                 "Unable_to_retrieve_processes");

        return;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        int pid;
        char process_name[128];

        if (sscanf(line, "%d %127s",
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
                        output_size - strlen(output) - 1);
            }

            strncat(output,
                    entry,
                    output_size - strlen(output) - 1);

            first = 0;
        }
    }

    pclose(file);
}

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    /*
     * Create TCP socket.
     */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    /*
     * Allow the port to be reused quickly.
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

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    /*
     * Bind socket to personalized port.
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
     * Start listening for Controllers.
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

    /*
     * Accept Controllers.
     */
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

        char buffer[1024];

        /*
         * The first command MUST be AUTH.
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
         * Build the expected authentication message.
         */
        char expected_auth[100];

        snprintf(expected_auth,
                 sizeof(expected_auth),
                 "AUTH %s\n",
                 AUTH_TOKEN);

        /*
         * Check authentication.
         */
        if (strcmp(buffer, expected_auth) == 0)
        {
            const char *response =
                "OK AUTHENTICATED SID:4322\n";

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            printf("Controller authenticated successfully\n");

            /*
             * Process commands after authentication.
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
                 * SYSINFO command
                 */
                if (strcmp(buffer, "SYSINFO\n") == 0)
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

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    printf("SYSINFO command processed\n");
                }

                /*
                 * LISTPROC command
                 */
                else if (strcmp(buffer, "LISTPROC\n") == 0)
                {
                    char process_list[2048];
                    char response[2200];

                    get_process_list(process_list,
                                     sizeof(process_list));

                    snprintf(response,
                             sizeof(response),
                             "OK PROCS %s SID:4322\n",
                             process_list);

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    printf("LISTPROC command processed\n");
                }

                /*
                 * QUIT command
                 */
                else if (strcmp(buffer, "QUIT\n") == 0)
                {
                    const char *response =
                        "OK BYE SID:4322\n";

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    printf("Controller requested disconnect\n");

                    break;
                }

                /*
                 * Unknown command
                 */
                else
                {
                    const char *response =
                        "ERR 003 UNKNOWN_COMMAND SID:4322\n";

                    send(client_fd,
                         response,
                         strlen(response),
                         0);
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

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            printf("Controller authentication failed\n");
        }

        close(client_fd);
    }

    close(server_fd);

    return 0;
}
