// server.c
// Central server: accepts up to 6 campus TCP connections and listens for UDP heartbeats.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

#define PORT 8080
#define UDP_PORT 9090
#define MAX_CAMPUSES 6
#define BUFFER_SIZE 2048
#define MAX_CLIENT_SLOTS 6
#define HEARTBEAT_TIMEOUT 20

// campus names and passwords
char campusses[MAX_CAMPUSES][10] = {"cfd","lhr","kar","pwr","mlt","isb"};
char passwords[MAX_CAMPUSES][15] = {"cfd123", "lhr123", "kar123", "pwr123", "mlt123", "isb123"};

// state
int campus_sock[MAX_CLIENT_SLOTS];    // TCP sockets for campuses (one slot per campus connection)
int step[MAX_CLIENT_SLOTS];           // 0: expect campus name, 1: expect password, 2: normal
int userIdx[MAX_CLIENT_SLOTS];        // which campus index is logged in on this slot (-1 if none)
time_t lastHeartbeat[MAX_CAMPUSES];   // last heartbeat time per campus
int campusOnline[MAX_CAMPUSES];       // 1 if campus is logged in (TCP + heartbeat supposed)

pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

// helper: parse dest campus from message of form "From ... To <campus>:... -> ..."
char* extracting_dest_campus(char* buf)
{
    static char campus[10];
    memset(campus,0,sizeof(campus));
    // Scan for "To <campus>:" pattern
    char *p = strstr(buf, " To ");
    if (!p) return campus;
    p += 4; // move past " To "
    // next token up to ':' is campus
    sscanf(p, "%[^:]", campus);
    return campus;
}

void* adminThread(void* arg)
{
    char cmd[BUFFER_SIZE];

    while (1)
    {
        printf("\n=> ");
        fflush(stdout);
        if (!fgets(cmd, sizeof(cmd), stdin)) continue;
        cmd[strcspn(cmd,"\n")] = 0;

        if (strcmp(cmd,"hb") == 0)
        {
            pthread_mutex_lock(&state_lock);
            printf("\n===== HEARTBEATS =====\n");
            for (int i=0;i<MAX_CAMPUSES;i++)
            {
                if (lastHeartbeat[i]==0)
                    printf("%s → NO HB\n", campusses[i]);
                else
                    printf("%s → %ld sec ago\n",
                           campusses[i], time(NULL)-lastHeartbeat[i]);
            }
            printf("======================\n");
            pthread_mutex_unlock(&state_lock);
            continue;
        }

        if (strncmp(cmd,"broadcast: ",11)==0)
        {
            pthread_mutex_lock(&state_lock);
            for (int i=0;i<MAX_CLIENT_SLOTS;i++)
            {
                if (campus_sock[i] > 0)
                {
                    ssize_t sent = send(campus_sock[i], cmd, strlen(cmd), 0);
                    if (sent < 0)
                        fprintf(stderr, "[ADMIN] send to campus slot %d failed: %s\n", i, strerror(errno));
                }
            }
            pthread_mutex_unlock(&state_lock);
            printf("[ADMIN] Broadcast sent to campuses.\n");
            continue;
        }

        printf("Unknown command.\n");
    }
    return NULL;
}

int find_campus_index_by_name(const char* name)
{
    for (int i=0;i<MAX_CAMPUSES;i++)
        if (strcmp(name, campusses[i])==0) return i;
    return -1;
}

int main()
{
    int tcp_fd, udp_fd, new_socket;
    struct sockaddr_in tcp_addr, udp_addr, udp_client;
    char buffer[BUFFER_SIZE];
    socklen_t udp_len = sizeof(udp_client);

    // init
    for (int i=0;i<MAX_CLIENT_SLOTS;i++) { campus_sock[i]=0; step[i]=0; userIdx[i]=-1; }
    for (int i=0;i<MAX_CAMPUSES;i++) { lastHeartbeat[i]=0; campusOnline[i]=0; }

    // create TCP listen socket
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) { perror("tcp socket"); exit(1); }

    int opt=1;
    setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&tcp_addr,0,sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(PORT);

    if (bind(tcp_fd, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr)) < 0) { perror("bind tcp"); exit(1); }
    if (listen(tcp_fd, MAX_CLIENT_SLOTS) < 0) { perror("listen"); exit(1); }

    printf("[SERVER] TCP running on port %d...\n", PORT);

    // create UDP socket for heartbeats
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) { perror("udp socket"); exit(1); }

    memset(&udp_addr,0,sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(UDP_PORT);

    if (bind(udp_fd, (struct sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) { perror("bind udp"); exit(1); }
    printf("[SERVER] UDP heartbeat listening on %d...\n", UDP_PORT);

    pthread_t admin;
    pthread_create(&admin, NULL, adminThread, NULL);

    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);

        FD_SET(tcp_fd, &readfds);
        FD_SET(udp_fd, &readfds);
        int max_sd = tcp_fd > udp_fd ? tcp_fd : udp_fd;

        pthread_mutex_lock(&state_lock);
        for (int i=0;i<MAX_CLIENT_SLOTS;i++)
        {
            if (campus_sock[i] > 0)
            {
                FD_SET(campus_sock[i], &readfds);
                if (campus_sock[i] > max_sd) max_sd = campus_sock[i];
            }
        }
        pthread_mutex_unlock(&state_lock);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select(max_sd + 1, &readfds, NULL, NULL, &tv);
        if (sel < 0 && errno!=EINTR) { perror("select"); }

        // UDP heartbeat
        if (FD_ISSET(udp_fd, &readfds))
        {
            memset(buffer,0,sizeof(buffer));
            int len = recvfrom(udp_fd, buffer, BUFFER_SIZE-1, 0, (struct sockaddr*)&udp_client, &udp_len);
            if (len > 0)
            {
                buffer[len]=0;
                // expected "HEARTBEAT from <campus>"
                char cname[32];
                if (sscanf(buffer, "HEARTBEAT from %31s", cname)==1)
                {
                    int idx = find_campus_index_by_name(cname);
                    if (idx != -1)
                    {
                        pthread_mutex_lock(&state_lock);
                        lastHeartbeat[idx] = time(NULL);
                        pthread_mutex_unlock(&state_lock);
                        // printf("[HB] %s received\n", cname);
                    }
                }
            }
        }

        // Timeout check
        pthread_mutex_lock(&state_lock);
        for (int c=0;c<MAX_CAMPUSES;c++)
        {
            if (lastHeartbeat[c]!=0 && (time(NULL) - lastHeartbeat[c] > HEARTBEAT_TIMEOUT))
            {
                printf("[TIMEOUT] %s offline (no hb for %ld sec)\n", campusses[c], time(NULL)-lastHeartbeat[c]);
                lastHeartbeat[c]=0;
                // disconnect if logged in
                for (int i=0;i<MAX_CLIENT_SLOTS;i++)
                {
                    if (userIdx[i]==c && campus_sock[i]>0)
                    {
                        close(campus_sock[i]);
                        campus_sock[i]=0;
                        step[i]=0;
                        userIdx[i]=-1;
                        campusOnline[c]=0;
                    }
                }
            }
        }
        pthread_mutex_unlock(&state_lock);

        // new TCP connection
        if (FD_ISSET(tcp_fd, &readfds))
        {
            struct sockaddr_in clientaddr;
            socklen_t addrlen = sizeof(clientaddr);
            new_socket = accept(tcp_fd, (struct sockaddr*)&clientaddr, &addrlen);
            if (new_socket >= 0)
            {
                pthread_mutex_lock(&state_lock);
                int placed=0;
                for (int i=0;i<MAX_CLIENT_SLOTS;i++)
                {
                    if (campus_sock[i]==0)
                    {
                        campus_sock[i]=new_socket;
                        step[i]=0;
                        userIdx[i]=-1;
                        placed=1;
                        break;
                    }
                }
                pthread_mutex_unlock(&state_lock);
                if (!placed)
                {
                    // refuse connection
                    const char *msg = "Server full\n";
                    send(new_socket, msg, strlen(msg), 0);
                    close(new_socket);
                }
                else
                {
                    printf("[TCP] New campus connection accepted (fd=%d)\n", new_socket);
                }
            }
        }

        // handle campus sockets
        pthread_mutex_lock(&state_lock);
        for (int i=0;i<MAX_CLIENT_SLOTS;i++)
        {
            int sd = campus_sock[i];
            if (sd==0) continue;
            if (!FD_ISSET(sd, &readfds)) continue;

            memset(buffer,0,sizeof(buffer));
            int n = read(sd, buffer, BUFFER_SIZE-1);
            if (n <= 0)
            {
                // closed
                if (userIdx[i] != -1)
                {
                    campusOnline[userIdx[i]] = 0;
                    lastHeartbeat[userIdx[i]] = 0;
                    printf("[TCP] Campus %s disconnected\n", campusses[userIdx[i]]);
                }
                close(sd);
                campus_sock[i]=0;
                step[i]=0;
                userIdx[i]=-1;
                continue;
            }
            buffer[n]=0;

            // LOGIN step
            if (step[i]==0)
            {
                int found = find_campus_index_by_name(buffer);
                if (found == -1)
                {
                    send(sd,"0",1,0); // invalid campus
                }
                else if (campusOnline[found])
                {
                    send(sd,"0",1,0); // already logged in
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

            if (step[i]==1)
            {
                int idx = userIdx[i];
                if (idx<0) { send(sd,"0",1,0); continue; }
                if (strcmp(buffer, passwords[idx])==0)
                {
                    send(sd,"Login Successful\n",17,0);
                    step[i]=2;
                    printf("[TCP] Campus %s logged in (slot %d, fd=%d)\n", campusses[idx], i, sd);
                }
                else
                {
                    send(sd,"0",1,0);
                }
                continue;
            }

            // Normal message handling
            // If this campus sends "broadcast: ..." it might come from admin as well, but server won't get broadcasts from campus normally.
            if (strncmp(buffer, "broadcast: ", 11) == 0)
            {
                // if a campus has forwarded a broadcast, forward to all other campuses as well
                for (int k=0;k<MAX_CLIENT_SLOTS;k++)
                {
                    if (campus_sock[k] > 0 && k != i)
                        send(campus_sock[k], buffer, strlen(buffer), 0);
                }
                continue;
            }

            // normal forwarding: parse dest campus name
            char *dest = extracting_dest_campus(buffer);
            if (strlen(dest)==0)
            {
                // Unknown format; ignore or log
                printf("[SERVER] Received malformed message: %s\n", buffer);
                continue;
            }
            int targetCampus = find_campus_index_by_name(dest);
            if (targetCampus == -1)
            {
                printf("[SERVER] Unknown dest campus: %s\n", dest);
                continue;
            }
            // find slot for that campus
            int targetSlot = -1;
            for (int k=0;k<MAX_CLIENT_SLOTS;k++)
            {
                if (campus_sock[k] > 0 && userIdx[k]==targetCampus)
                {
                    targetSlot = k;
                    break;
                }
            }
            if (targetSlot != -1)
            {
                send(campus_sock[targetSlot], buffer, strlen(buffer), 0);
            }
            else
            {
                printf("[FORWARD] %s not online\n", dest);
            }
        }
        pthread_mutex_unlock(&state_lock);
    }

    return 0;
}
