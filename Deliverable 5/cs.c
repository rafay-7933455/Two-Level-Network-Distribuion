// central.c  (server.c)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

#define PORT 8080
#define UDP_PORT 9090         // heartbeats arrive here
#define MAX_CLIENTS 6
#define BUFFER_SIZE 2048

time_t lastHeartbeat[MAX_CLIENTS];
int udp_fd = -1;  // heartbeat recv socket
int bcast_sock = -1; // admin broadcast sender socket

// campuses ordering (must stay consistent across files)
const char campusses_global[6][10] = {"cfd","lhr","kar","pwr","mlt","isb"};
const int campus_pc_ports[6] = {7000,7001,7002,7003,7004,7005}; // PC ports (last can be unused)
const int campus_admin_ports[6] = {9100,9101,9102,9103,9104,9105}; // admin/broadcast recv ports per campus

//--------------------------------------------------
// Parsing helpers (use " to " - lowercase)
//--------------------------------------------------

// Copies the token after " to " into out (maxlen). Returns 0 on success, -1 on fail.
static int get_after_to(const char *buf, char *out, size_t maxlen) {
    const char *p = strstr(buf, " to ");
    if (!p) return -1;
    p += 4; // skip " to "
    // copy until newline or end
    size_t i = 0;
    while (*p && *p != '\r' && *p != '\n' && i + 1 < maxlen) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

// Extract dept (first token before ':') from the "after to" piece.
// caller owns dest (size >=4)
char* extracting_dest_dept(char* buf) {
    static char dept[8];
    dept[0]=0;
    char after[256];
    if (get_after_to(buf, after, sizeof(after)) == 0) {
        // expected after like "AD:lhr:hamza"
        char *p = strchr(after, ':');
        if (p) {
            size_t len = p - after;
            if (len >= sizeof(dept)) len = sizeof(dept)-1;
            strncpy(dept, after, len);
            dept[len]=0;
            return dept;
        }
    }
    return NULL;
}

// Extract campus (second field) from after-to piece. returns pointer to static buffer or NULL
char* extracting_dest_campus(char* buf) {
    static char campus[32];
    campus[0]=0;
    char after[256];
    if (get_after_to(buf, after, sizeof(after)) == 0) {
        // after: "<DEPT>:<campus>:<name>"
        char dept[64], name[128];
        int scanned = sscanf(after, "%63[^:]:%31[^:]:%127s", dept, campus, name);
        if (scanned >= 2) return campus;
    }
    return NULL;
}

// Extract name (third field) from after-to piece.
char* extracting_dest_name(char* buf) {
    static char name[128];
    name[0]=0;
    char after[256];
    if (get_after_to(buf, after, sizeof(after)) == 0) {
        char dept[64], campus[64];
        int scanned = sscanf(after, "%63[^:]:%63[^:]:%127s", dept, campus, name);
        if (scanned == 3) return name;
    }
    return NULL;
}

//--------------------------------------------------
// ADMIN MODULE (thread)
//--------------------------------------------------
void* admin_module(void* arg)
{
    // make bcast_sock non-blocking? we will send synchronously to specific admin ports
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    char adminMsg[1024];

    struct sockaddr_in campus_addr;
    memset(&campus_addr, 0, sizeof(campus_addr));
    campus_addr.sin_family = AF_INET;
    campus_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    while (1)
    {
        printf("\n\n===== ADMIN PANEL =====\n");
        printf("1. Show Campus Status\n");
        printf("2. Broadcast Announcement\n");
        printf("3. Exit Admin Panel\n");
        printf("Select: ");

        int choice;
        if (scanf("%d", &choice) != 1) {
            int c; while ((c = getchar()) != EOF && c != '\n');
            continue;
        }
        getchar(); // consume newline

        if (choice == 1)
        {
            printf("\n--- Campus Status ---\n");
            for (int i = 0; i < 6; i++)
            {
                if (lastHeartbeat[i] == 0)
                    printf("%s : OFFLINE\n", campusses_global[i]);
                else
                    printf("%s : ONLINE (last seen %ld sec ago)\n",
                           campusses_global[i],
                           (long)(time(NULL) - lastHeartbeat[i]));
            }
        }
        else if (choice == 2)
        {
            printf("Enter announcement: ");
            if (!fgets(adminMsg, sizeof(adminMsg), stdin)) {
                printf("[ADMIN] no input\n");
                continue;
            }
            adminMsg[strcspn(adminMsg, "\n")] = 0;
            printf("[ADMIN] Broadcasting: %s\n", adminMsg);

            for (int i = 0; i < 6; i++)
            {
                campus_addr.sin_port = htons(campus_admin_ports[i]);

                char combined[2048];
                // message format: "BC(Admin):ISB to All: <message>"
                snprintf(combined, sizeof(combined),
                         "BC(Admin):ISB to All:%s", adminMsg);

                ssize_t sent = sendto(bcast_sock,
                                      combined,
                                      strlen(combined),
                                      0,
                                      (struct sockaddr*)&campus_addr,
                                      sizeof(campus_addr));
                if (sent < 0) {
                    fprintf(stderr, "[ADMIN] sendto -> %s:%d failed: %s\n",
                            inet_ntoa(campus_addr.sin_addr),
                            campus_admin_ports[i], strerror(errno));
                } else {
                    printf("[ADMIN] sent %zd bytes to %s:%d\n", sent,
                           inet_ntoa(campus_addr.sin_addr), campus_admin_ports[i]);
                }
            }
            printf("[ADMIN] Broadcast complete.\n");
        }
        else if (choice == 3)
        {
            printf("Exiting admin panel...\n");
            pthread_exit(NULL);
        }
        else {
            printf("Invalid option.\n");
        }
    }
    return NULL;
}

//--------------------------------------------------
// MAIN SERVER
//--------------------------------------------------
int main()
{
    int tcp_fd, new_socket;
    int client_socket[MAX_CLIENTS] = {0};
    int step[MAX_CLIENTS] = {0};
    int userIdx[MAX_CLIENTS];

    struct sockaddr_in tcp_addr, udp_addr, udp_client_addr;

    char buffer[BUFFER_SIZE];
    socklen_t udp_len = sizeof(udp_client_addr);

    char campusses[6][10] = {"cfd", "lhr", "kar", "pwr", "mlt", "isb"};
    char passwords[6][15] = {"cfd123", "lhr123", "kar123", "pwr123", "mlt123", "isb123"};

    for (int i = 0; i < MAX_CLIENTS; i++) userIdx[i] = -1;
    for (int i = 0; i < 6; i++) lastHeartbeat[i] = 0;

    // ---------------- TCP ----------------
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) { perror("tcp socket"); return 1; }

    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(PORT);

    if (bind(tcp_fd, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("tcp bind");
        close(tcp_fd);
        return 1;
    }
    if (listen(tcp_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(tcp_fd);
        return 1;
    }

    printf("[SERVER] TCP running on %d\n", PORT);

    // ---------------- UDP (heartbeat recv) ----------------
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) { perror("udp socket"); close(tcp_fd); return 1; }

    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(UDP_PORT);

    if (bind(udp_fd, (struct sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("udp bind");
        close(tcp_fd);
        close(udp_fd);
        return 1;
    }

    printf("[SERVER] UDP (heartbeats) running on %d\n", UDP_PORT);

    // ---------------- UDP (admin broadcast sender socket) ----------------
    bcast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (bcast_sock < 0) { perror("bcast socket"); close(tcp_fd); close(udp_fd); return 1; }
    // no bind required for sending; we keep it simple

    // ------------------------------------------
    // Start ADMIN THREAD
    // ------------------------------------------
    pthread_t adminThread;
    if (pthread_create(&adminThread, NULL, admin_module, NULL) != 0) {
        perror("pthread_create admin");
        close(tcp_fd);
        close(udp_fd);
        close(bcast_sock);
        return 1;
    }

    //------------------------------------------------------
    // MAIN LOOP
    //------------------------------------------------------
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

        int rv = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (rv < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // HEARTBEAT handling
        if (FD_ISSET(udp_fd, &readfds))
        {
            memset(buffer, 0, BUFFER_SIZE);
            int len = recvfrom(udp_fd, buffer, BUFFER_SIZE - 1, 0,
                               (struct sockaddr*)&udp_client_addr, &udp_len);

            if (len > 0)
            {
                buffer[len] = '\0';
                // expect "HEARTBEAT from <campus>"
                char campus[64] = {0};
                sscanf(buffer, "HEARTBEAT from %63s", campus);
                for (int c = 0; c < 6; c++)
                {
                    if (strcmp(campusses[c], campus) == 0)
                    {
                        lastHeartbeat[c] = time(NULL);
                        // printf("[HEARTBEAT] %s at %ld\n", campus, (long)lastHeartbeat[c]);
                        break;
                    }
                }
            }
        }

        // TIMEOUT check
        for (int c = 0; c < 6; c++)
        {
            if (lastHeartbeat[c] == 0) continue;
            if (time(NULL) - lastHeartbeat[c] > 20)
            {
                printf("[TIMEOUT] Campus %s offline\n", campusses[c]);
                for (int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (client_socket[i] != 0 && userIdx[i] == c)
                    {
                        printf("[SERVER] closing tcp client %d for campus %s\n", i, campusses[c]);
                        close(client_socket[i]);
                        client_socket[i] = 0;
                        step[i] = 0;
                        userIdx[i] = -1;
                    }
                }
                lastHeartbeat[c] = 0;
            }
        }

        // NEW TCP client
        if (FD_ISSET(tcp_fd, &readfds))
        {
            int addrlen = sizeof(tcp_addr);
            new_socket = accept(tcp_fd, (struct sockaddr*)&tcp_addr, (socklen_t*)&addrlen);
            if (new_socket < 0) {
                perror("accept");
            } else {
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
        }

        // CLIENT handling
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            int sd = client_socket[i];
            if (sd == 0) continue;
            if (!FD_ISSET(sd, &readfds)) continue;

            memset(buffer, 0, BUFFER_SIZE);
            int val = read(sd, buffer, BUFFER_SIZE - 1);
            if (val <= 0)
            {
                close(sd);
                client_socket[i] = 0;
                step[i] = 0;
                userIdx[i] = -1;
                continue;
            }
            buffer[val] = '\0';

            // LOGIN
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
                if (found == -1) send(sd, "0", 1, 0);
                else {
                    userIdx[i] = found;
                    send(sd, "1", 1, 0);
                    step[i] = 1;
                }
                continue;
            }

            // PASSWORD
            if (step[i] == 1)
            {
                if (strcmp(buffer, passwords[userIdx[i]]) == 0)
                {
                    send(sd, "Login Successful\n", 18, 0);
                    step[i] = 2;
                }
                else send(sd, "0", 1, 0);
                continue;
            }

            // FORWARDING
            char *destDept   = extracting_dest_dept(buffer);
            char *destCampus = extracting_dest_campus(buffer);
            char *destName   = extracting_dest_name(buffer);

            if (destCampus == NULL) continue;

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
            if (target != -1) {
                send(client_socket[target], buffer, strlen(buffer), 0);
            } else {
                // not online - optionally inform sender
                char note[128];
                snprintf(note, sizeof(note), "DEST_OFFLINE: %s\n", destCampus);
                send(sd, note, strlen(note), 0);
            }
        }
    }

    // cleanup
    pthread_cancel(adminThread);
    pthread_join(adminThread, NULL);
    close(tcp_fd);
    close(udp_fd);
    close(bcast_sock);
    return 0;
}
