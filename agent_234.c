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

/* Get current system information */
void get_sysinfo(double *cpu_load, long *mem_used_mb, long *uptime_sec)
{
    FILE *file;
    char line[256];

    /* CPU load from /proc/loadavg */
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

    /* Memory usage from /proc/meminfo */
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

            if (sscanf(line, "MemAvailable: %ld kB", &mem_available) == 1)
            {
                continue;
            }
        }

        fclose(file);
    }

    long mem_used = mem_total - mem_available;
    *mem_used_mb = mem_used / 1024;

    /* Uptime from /proc/uptime */
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

        char buffer[1024];

        /* First command MUST be AUTH */
        int bytes_received = receive_line(client_fd,
                                          buffer,
                                          sizeof(buffer));

        if (bytes_received <= 0)
        {
            printf("Controller disconnected before authentication\n");
            close(client_fd);
            continue;
        }

        char expected_auth[100];

        snprintf(expected_auth,
                 sizeof(expected_auth),
                 "AUTH %s\n",
                 AUTH_TOKEN);

        if (strcmp(buffer, expected_auth) == 0)
        {
            const char *response =
                "OK AUTHENTICATED SID:4322\n";

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            printf("Controller authenticated successfully\n");

            /* Receive commands after authentication */
            while (1)
            {
                bytes_received = receive_line(client_fd,
                                              buffer,
                                              sizeof(buffer));

                if (bytes_received <= 0)
                {
                    printf("Controller disconnected\n");
                    break;
                }

                /* SYSINFO command */
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
