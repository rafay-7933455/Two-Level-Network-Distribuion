// server.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT 8080
#define UDP_PORT 9090
#define MAX_CLIENTS 6
#define BUFFER_SIZE 2048
#define HOST_CAMPUS "ISB"

char* extracting_dest(char* buf){
    char dest[4] = {'\0'};
    dest[0] = buf[10];
    dest[1] = buf[11];
    dest[2] = buf[12];
    //cfd,  lhr,  kar,  pwr,  mlt
    // return dest;
    if(strcmp(dest, "cfd")==0) return "cfd";
    else if(strcmp(dest, "lhr")==0) return "lhr";
    else if(strcmp(dest, "kar")==0) return "kar";
    else if(strcmp(dest, "pwr")==0) return "pwr";
    else if(strcmp(dest, "mlt")==0) return "mlt";
    else return NULL;
}

int main() {
    int tcp_fd, udp_fd, new_socket;
    int client_socket[MAX_CLIENTS] = {0};
    int step[MAX_CLIENTS] = {0};
    int userIdx[MAX_CLIENTS];
    struct sockaddr_in tcp_addr, udp_addr, udp_client_addr;

    char buffer[BUFFER_SIZE];
    socklen_t udp_len = sizeof(udp_client_addr);

    // campus + password lists
    char campusses[6][10] = {"cfd", "isb", "khi", "multan", "lhr", "karachi"};
    char passwords[6][15] = {"cfd123", "isb123", "khi123", "multan123", "lhr123", "karachi123"};

    // initialize user indices
    for (int i = 0; i < MAX_CLIENTS; i++)
        userIdx[i] = -1;

    // ---------------------------- TCP SOCKET -----------------------------
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);

    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(PORT);

    bind(tcp_fd, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr));
    listen(tcp_fd, MAX_CLIENTS);
    printf("[SERVER] TCP running on %d\n", PORT);

    // ---------------------------- UDP SOCKET -----------------------------
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(UDP_PORT);

    bind(udp_fd, (struct sockaddr*)&udp_addr, sizeof(udp_addr));
    printf("[SERVER] UDP running on %d\n", UDP_PORT);

    // ============================ MAIN LOOP ===============================
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);

        FD_SET(tcp_fd, &readfds);
        FD_SET(udp_fd, &readfds);

        int max_sd = (tcp_fd > udp_fd) ? tcp_fd : udp_fd;

        // add clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_socket[i] > 0) {
                FD_SET(client_socket[i], &readfds);
                if (client_socket[i] > max_sd)
                    max_sd = client_socket[i];
            }
        }

        select(max_sd + 1, &readfds, NULL, NULL, NULL);

        // ======================= UDP HEARTBEAT ============================
        if (FD_ISSET(udp_fd, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            int len = recvfrom(udp_fd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&udp_client_addr, &udp_len);
            if (len > 0) {
                buffer[len] = '\0';
                printf("\n[UDP] %s on Port %d\n", buffer, ntohs(udp_client_addr.sin_port));
                // printf("\n[UDP] %s:%d → %s\n", inet_ntoa(udp_client_addr.sin_addr), ntohs(udp_client_addr.sin_port), buffer);
            }
        }

        // ======================= NEW TCP CLIENT ===========================
        if (FD_ISSET(tcp_fd, &readfds)) {
            int addrlen = sizeof(tcp_addr);
            new_socket = accept(tcp_fd, (struct sockaddr*)&tcp_addr, (socklen_t*)&addrlen);

            printf("[TCP] New connection\n");

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_socket[i] == 0) {
                    client_socket[i] = new_socket;
                    step[i] = 0;
                    userIdx[i] = -1;
                    break;
                }
            }
        }

        // =================== HANDLE TCP CLIENTS ===========================
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_socket[i];
            if (sd == 0) continue;

            if (!FD_ISSET(sd, &readfds)) continue;
            
            memset(buffer, 0, BUFFER_SIZE);
            int val = read(sd, buffer, BUFFER_SIZE - 1);

            if (val <= 0) {
                printf("[TCP] Client disconnected\n");
                close(sd);
                client_socket[i] = 0;
                step[i] = 0;
                userIdx[i] = -1;
                continue;
            }

            buffer[val] = '\0';
            buffer[strcspn(buffer, "\n")] = 0;

            // ---------------- CAMPUS LOGIN ----------------
            if (step[i] == 0) {
                int found = -1;
                for (int c = 0; c < 6; c++) {
                    if (strcmp(buffer, campusses[c]) == 0) {
                        found = c;
                        break;
                    }
                }

                if (found == -1) {
                    send(sd, "0", 1, 0);
                } else {
                    userIdx[i] = found;
                    send(sd, "1", 1, 0);
                    step[i] = 1;
                }
                continue;
            }

            // ---------------- PASSWORD CHECK ----------------
            if (step[i] == 1) {
                if (strcmp(buffer, passwords[userIdx[i]]) == 0) {
                    send(sd, "Login Successful\n", 18, 0);
                    step[i] = 2;
                } else {
                    send(sd, "0", 1, 0);
                }
                continue;
            }

            // ---------------- NORMAL ECHO MODE ----------------
            // // printf("%s\n", buffer);
            // printf("\n[TCP] Client-%d:%s\n", userIdx[i], buffer);
            // send(sd, buffer, strlen(buffer), 0);

            //exit check

            // if(strcmp(buffer,"exit")==0) {
            //     printf("[TCP] Client disconnected\n");
            //     close(sd);
            //     client_socket[i] = 0;
            //     step[i] = 0;
            //     userIdx[i] = -1;
            //     continue;
            // }
            // Forwarding
            printf("\n[TCP] Client-%d:%s\n", userIdx[i], buffer);

            // extract destination campus (cfd, lhr, kar, pwr, mlt, etc.)
            char* dest = extracting_dest(buffer);
            if(strcmp(dest, "isb")==0){

            }
            else if(dest!=NULL && strcmp(dest, "isb")!=0){
                // find the client index that belongs to that campus
                int target = -1;

                for (int k = 0; k < MAX_CLIENTS; k++) {
                    if (client_socket[k] != 0 && userIdx[k] != -1) {
                        if (strcmp(campusses[userIdx[k]], dest) == 0) {
                            target = k;
                            break;
                        }
                    }
                }

                // forward the message if target online
                if (target != -1) {
                    send(client_socket[target], buffer, strlen(buffer), 0);
                    printf("[FORWARD] Message sent from %s to %s\n",
                        campusses[userIdx[i]], dest);
                } else {
                    printf("[FORWARD] Destination %s not online\n", dest);
                }

                
                // printf("%s", dest);
            }
            else printf("\nInvalid Campus\n");

        }
    }

    return 0;
}
