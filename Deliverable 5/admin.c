// ==================== server.c (CENTRAL SERVER) ====================

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>
#include <pthread.h>

#define PORT 8080
#define UDP_PORT 9090
#define MAX_CLIENTS 6
#define BUFFER_SIZE 2048

// ===================== GLOBALS =====================
time_t lastHeartbeat[6];

char campusses[6][10] = {"cfd","lhr","kar","pwr","mlt","isb"};
char passwords[6][15] = {"cfd123", "lhr123", "kar123", "pwr123", "mlt123", "isb123"};

// client info
int client_socket[MAX_CLIENTS] = {0};
int step[MAX_CLIENTS] = {0};
int userIdx[MAX_CLIENTS] = {-1};

// ✅ Each campus must connect uniquely
int campusOnline[6] = {0};

// ================= PARSING =================
char* extracting_dest_dept(char* buf)
{
    static char dept[4];
    memset(dept,0,sizeof(dept));
    sscanf(buf,"%*s %*s To %[^:]:%*[^:]:%*s", dept);
    return dept;
}

char* extracting_dest_campus(char* buf)
{
    static char campus[10];
    memset(campus,0,sizeof(campus));
    sscanf(buf,"%*s %*s To %*[^:]:%[^:]:%*s", campus);
    return campus;
}

char* extracting_dest_name(char* buf)
{
    static char name[20];
    memset(name,0,sizeof(name));
    sscanf(buf,"%*s %*s To %*[^:]:%*[^:]:%s", name);
    return name;
}



// ==================== ADMIN THREAD ====================
void* adminThread(void* arg)
{
    char cmd[200];

    while (1)
    {
        printf("\n=> ");
        fflush(stdout);
        fgets(cmd, sizeof(cmd), stdin);
        cmd[strcspn(cmd,"\n")] = 0;

        if (strcmp(cmd,"hb") == 0)
        {
            printf("\n===== HEARTBEATS =====\n");
            for (int i=0;i<6;i++)
            {
                if (lastHeartbeat[i]==0)
                    printf("%s → NO HB\n", campusses[i]);
                else
                    printf("%s → %ld sec ago\n",
                           campusses[i], time(NULL)-lastHeartbeat[i]);
            }
            printf("======================\n");
            continue;
        }

        if (strncmp(cmd,"broadcast: ",11)==0)
        {
            printf("[ADMIN] Broadcast: %s\n", cmd);

            for (int i=0;i<MAX_CLIENTS;i++)
            {
                if (client_socket[i]>0)
                    send(client_socket[i], cmd, strlen(cmd), 0);
            }

            continue;
        }

        printf("Unknown command.\n");
    }

    return NULL;
}



// ==================== MAIN ====================
int main()
{
    int tcp_fd, udp_fd, new_socket;

    struct sockaddr_in tcp_addr, udp_addr, udp_client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t udp_len = sizeof(udp_client_addr);

    // init
    for (int i=0;i<6;i++)
    {
        lastHeartbeat[i] = 0;
        campusOnline[i] = 0;
    }

    for (int i=0;i<MAX_CLIENTS;i++)
        userIdx[i] = -1;

    // --------------- TCP -----------------
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);

    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(PORT);

    bind(tcp_fd, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr));
    listen(tcp_fd, MAX_CLIENTS);

    printf("[SERVER] TCP running...\n");

    // --------------- UDP -----------------
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(UDP_PORT);

    bind(udp_fd, (struct sockaddr*)&udp_addr, sizeof(udp_addr));

    printf("[SERVER] UDP heartbeat running...\n");

    pthread_t admin;
    pthread_create(&admin, NULL, adminThread, NULL);



    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);

        FD_SET(tcp_fd, &readfds);
        FD_SET(udp_fd, &readfds);

        int max_sd = tcp_fd > udp_fd ? tcp_fd : udp_fd;

        for (int i=0;i<MAX_CLIENTS;i++)
        {
            if (client_socket[i] > 0)
            {
                FD_SET(client_socket[i], &readfds);
                if (client_socket[i] > max_sd)
                    max_sd = client_socket[i];
            }
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        select(max_sd + 1, &readfds, NULL, NULL, &tv);



        // ================= HEARTBEAT =================
        if (FD_ISSET(udp_fd, &readfds))
        {
            memset(buffer,0,BUFFER_SIZE);

            int len = recvfrom(udp_fd, buffer, BUFFER_SIZE-1, 0,
                               (struct sockaddr*)&udp_client_addr, &udp_len);

            if (len > 0)
            {
                buffer[len] = 0;

                char cname[20];
                sscanf(buffer, "HEARTBEAT from %s", cname);

                for (int c=0;c<6;c++)
                {
                    if (strcmp(cname,campusses[c])==0)
                        lastHeartbeat[c] = time(NULL);
                }
            }
        }



        // ================= TIMEOUT =================
        for (int c=0;c<6;c++)
        {
            if (lastHeartbeat[c]!=0 &&
                time(NULL) - lastHeartbeat[c] > 20)
            {
                printf("[TIMEOUT] %s offline\n", campusses[c]);

                lastHeartbeat[c] = 0;

                for (int i=0;i<MAX_CLIENTS;i++)
                {
                    if (client_socket[i]>0 && userIdx[i]==c)
                    {
                        close(client_socket[i]);
                        client_socket[i]=0;

                        step[i]=0;
                        userIdx[i]=-1;

                        campusOnline[c]=0;
                    }
                }
            }
        }



        // ================= NEW TCP CONNECTION =================
        if (FD_ISSET(tcp_fd, &readfds))
        {
            int addrlen = sizeof(tcp_addr);

            new_socket = accept(tcp_fd, (struct sockaddr*)&tcp_addr,
                                (socklen_t*)&addrlen);

            printf("[TCP] New connection\n");

            for (int i=0;i<MAX_CLIENTS;i++)
            {
                if (client_socket[i]==0)
                {
                    client_socket[i]=new_socket;
                    step[i]=0;
                    userIdx[i]=-1;
                    break;
                }
            }
        }



        // ================= HANDLE CLIENTS =================
        for (int i=0;i<MAX_CLIENTS;i++)
        {
            int sd = client_socket[i];

            if (sd==0 || !FD_ISSET(sd, &readfds))
                continue;

            memset(buffer,0,BUFFER_SIZE);
            int n = read(sd, buffer, BUFFER_SIZE-1);

            if (n <= 0)
            {
                if (userIdx[i]!=-1)
                    campusOnline[userIdx[i]] = 0;

                close(sd);
                client_socket[i]=0;
                step[i]=0;
                userIdx[i]=-1;

                continue;
            }

            buffer[n]=0;



            // ================= LOGIN =================
            if (step[i]==0)
            {
                int found=-1;

                for (int c=0;c<6;c++)
                {
                    if (strcmp(buffer,campusses[c])==0)
                    {
                        found=c;
                        break;
                    }
                }

                if (found == -1)               // invalid campus
                {
                    send(sd,"0",1,0);
                }
                else if (campusOnline[found])  // already logged in
                {
                    send(sd,"0",1,0);
                }
                else
                {
                    userIdx[i]=found;
                    campusOnline[found]=1;

                    send(sd,"1",1,0);
                    step[i]=1;
                }

                continue;
            }



            // ================= PASSWORD =================
            if (step[i]==1)
            {
                if (strcmp(buffer, passwords[userIdx[i]])==0)
                {
                    send(sd,"Login Successful\n",18,0);
                    step[i]=2;
                }
                else
                {
                    send(sd,"0",1,0);
                }

                continue;
            }



            // ================= MESSAGE FORWARDING =================
            char *destCampus = extracting_dest_campus(buffer);

            int target=-1;

            for (int k=0;k<MAX_CLIENTS;k++)
            {
                if (client_socket[k]!=0 &&
                    userIdx[k]!=-1 &&
                    strcmp(campusses[userIdx[k]], destCampus)==0)
                {
                    target=k;
                    break;
                }
            }

            if (target!=-1)
                send(client_socket[target], buffer, strlen(buffer), 0);
            else
                printf("[FORWARD] %s not online\n", destCampus);
        }
    }

    return 0;
}
