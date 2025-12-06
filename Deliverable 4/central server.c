// central.c  (server.c)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>

#define PORT 8080
#define UDP_PORT 9090
#define MAX_CLIENTS 6
#define BUFFER_SIZE 2048

time_t lastHeartbeat[MAX_CLIENTS];

char* extracting_dest_dept(char* buf)
{
    static char dept[4];
    memset(dept, 0, sizeof(dept));

    // From IT:cfd:ali To AD:lhr:hamza
    sscanf(buf,
           "%*s %*s To %[^:]:%*[^:]:%*s",
           dept);

    return dept;
}

char* extracting_dest_campus(char* buf)
{
    static char campus[10];
    memset(campus, 0, sizeof(campus));

    sscanf(buf,
           "%*s %*s To %*[^:]:%[^:]:%*s",
           campus);

    return campus;
}

char* extracting_dest_name(char* buf)
{
    static char name[20];
    memset(name, 0, sizeof(name));

    sscanf(buf,
           "%*s %*s To %*[^:]:%*[^:]:%s",
           name);

    return name;
}

char* extracting_dest(char* buf)
{
    // old campus extraction, used for routing
    static char dest[4];
    memset(dest, 0, sizeof(dest));

    dest[0] = buf[10];
    dest[1] = buf[11];
    dest[2] = buf[12];

    if (strcmp(dest, "cfd") == 0) return "cfd";
    if (strcmp(dest, "lhr") == 0) return "lhr";
    if (strcmp(dest, "kar") == 0) return "kar";
    if (strcmp(dest, "pwr") == 0) return "pwr";
    if (strcmp(dest, "mlt") == 0) return "mlt";
    return NULL;
}

int main()
{
    int tcp_fd, udp_fd, new_socket;
    int client_socket[MAX_CLIENTS] = {0};
    int step[MAX_CLIENTS] = {0};
    int userIdx[MAX_CLIENTS];

    struct sockaddr_in tcp_addr, udp_addr, udp_client_addr;

    char buffer[BUFFER_SIZE];
    socklen_t udp_len = sizeof(udp_client_addr);

    char campusses[6][10] = {"cfd", "isb", "khi", "multan", "lhr", "karachi"};
    char passwords[6][15] = {"cfd123", "isb123", "khi123", "multan123", "lhr123", "karachi123"};

    for (int i = 0; i < MAX_CLIENTS; i++) userIdx[i] = -1;
    for (int i = 0; i < 6; i++) lastHeartbeat[i] = 0;

    // TCP socket
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);

    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(PORT);

    bind(tcp_fd, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr));
    listen(tcp_fd, MAX_CLIENTS);

    printf("[SERVER] TCP running\n");

    // UDP socket
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(UDP_PORT);

    bind(udp_fd, (struct sockaddr*)&udp_addr, sizeof(udp_addr));

    printf("[SERVER] UDP running\n");

    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);

        FD_SET(tcp_fd, &readfds);
        FD_SET(udp_fd, &readfds);

        int max_sd = (tcp_fd > udp_fd) ? tcp_fd : udp_fd;

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (client_socket[i] > 0)
            {
                FD_SET(client_socket[i], &readfds);
                if (client_socket[i] > max_sd)
                    max_sd = client_socket[i];
            }
        }

        select(max_sd + 1, &readfds, NULL, NULL, NULL);

        // UDP heartbeat
        if (FD_ISSET(udp_fd, &readfds))
        {
            memset(buffer, 0, BUFFER_SIZE);
            int len = recvfrom(udp_fd, buffer, BUFFER_SIZE - 1, 0,
                            (struct sockaddr*)&udp_client_addr, &udp_len);

            if (len > 0)
            {
                buffer[len] = '\0';
                printf("\n[UDP] %s\n", buffer);

                // Example heartbeat: "HEARTBEAT from cfd"
                char campus[20];
                sscanf(buffer, "HEARTBEAT from %s", campus);

                // find index of campus
                for (int c = 0; c < 6; c++)
                {
                    if (strcmp(campusses[c], campus) == 0)
                    {
                        lastHeartbeat[c] = time(NULL);  // store current time
                        printf("[HEARTBEAT] %s updated at %ld\n",
                            campus, lastHeartbeat[c]);
                        break;
                    }
                }
            }
        }
        // --- CHECK HEARTBEAT TIMEOUTS ---
        for (int c = 0; c < 6; c++)
        {
            if (lastHeartbeat[c] == 0) continue;

            if (time(NULL) - lastHeartbeat[c] > 20)
            {
                printf("[TIMEOUT] Campus %s is offline!\n", campusses[c]);

                // find TCP client belonging to this campus
                for (int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (client_socket[i] != 0 && userIdx[i] == c)
                    {
                        printf("[TCP] Disconnecting client %d (campus %s)\n",
                            i, campusses[c]);

                        close(client_socket[i]);
                        client_socket[i] = 0;
                        step[i] = 0;
                        userIdx[i] = -1;
                    }
                }

                lastHeartbeat[c] = 0;  // prevent repeating the message
            }
        }


        // New TCP client
        if (FD_ISSET(tcp_fd, &readfds))
        {
            int addrlen = sizeof(tcp_addr);
            new_socket = accept(tcp_fd, (struct sockaddr*)&tcp_addr, (socklen_t*)&addrlen);

            printf("[TCP] New connection\n");

            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (client_socket[i] == 0)
                {
                    client_socket[i] = new_socket;
                    step[i] = 0;
                    userIdx[i] = -1;
                    break;
                }
            }
        }

        // Handle clients
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            int sd = client_socket[i];
            if (sd == 0) continue;

            if (!FD_ISSET(sd, &readfds)) continue;

            memset(buffer, 0, BUFFER_SIZE);
            int val = read(sd, buffer, BUFFER_SIZE - 1);

            if (val <= 0)
            {
                printf("[TCP] Client disconnected\n");
                close(sd);
                client_socket[i] = 0;
                step[i] = 0;
                userIdx[i] = -1;
                continue;
            }

            buffer[val] = '\0';

            // ---------------- LOGIN -------------------
            if (step[i] == 0)
            {
                int found = -1;
                for (int c = 0; c < 6; c++)
                {
                    if (strcmp(buffer, campusses[c]) == 0)
                    {
                        found = c;
                        break;
                    }
                }

                if (found == -1)
                    send(sd, "0", 1, 0);
                else {
                    userIdx[i] = found;
                    send(sd, "1", 1, 0);
                    step[i] = 1;
                }

                continue;
            }

            // ---------------- PASSWORD -------------------
            if (step[i] == 1)
            {
                if (strcmp(buffer, passwords[userIdx[i]]) == 0)
                {
                    send(sd, "Login Successful\n", 18, 0);
                    step[i] = 2;
                }
                else {
                    send(sd, "0", 1, 0);
                }

                continue;
            }

            // ---------------- FORWARDING -------------------
            printf("\n[TCP] Client-%d: %s\n", userIdx[i], buffer);

            char *destDept   = extracting_dest_dept(buffer);
            char *destCampus = extracting_dest_campus(buffer);
            char *destName   = extracting_dest_name(buffer);

            printf("[PARSED] dept=%s campus=%s name=%s\n",
                   destDept, destCampus, destName);

            if (destCampus == NULL)
            {
                printf("[ERROR] No campus found\n");
                continue;
            }

            if (strcmp(destCampus, "isb") == 0)
            {
                printf("[INFO] Message for ISB – central\n");
                continue;
            }

            // Find matching campus client
            int target = -1;

            for (int k = 0; k < MAX_CLIENTS; k++)
            {
                if (client_socket[k] != 0 && userIdx[k] != -1)
                {
                    if (strcmp(campusses[userIdx[k]], destCampus) == 0)
                    {
                        target = k;
                        break;
                    }
                }
            }

            if (target != -1)
            {
                send(client_socket[target], buffer, strlen(buffer), 0);

                printf("[FORWARD] %s (%s) → %s\n",
                       destDept, destCampus, destName);
            }
            else {
                printf("[FORWARD] Destination %s not online\n", destCampus);
            }
        }
    }

    return 0;
}
