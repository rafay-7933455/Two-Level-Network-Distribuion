// campus_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h>

#define TCP_PORT 8080        // Central server port
#define UDP_PORT 9090        // Heartbeat port
// #define PC_PORT 7000         // <-- NEW: PC clients connect to this
#define BUFFER_SIZE 2048

int PC_PORT = 7000;
const int CAMPUS_PORTS[5] = {
  //cfd,  lhr,  kar,  pwr,  mlt
    7000, 7001, 7002, 7003, 7004
};

typedef struct {
    int udp_sock;
    struct sockaddr_in udp_addr;
    char campus[20];
} hb_args_t;

char* extracting_dest_dept(char* buf){
    char dest[4] = {'\0'};
    dest[0] = buf[15];
    dest[1] = buf[16];

    if(strcmp(dest, "IT")==0 || strcmp(dest, "it")==0 || strcmp(dest, "It")==0) return "IT";
    else if(strcmp(dest, "Ad")==0 || strcmp(dest, "AD")==0 || strcmp(dest, "ad")==0) return "Ad";
    else if(strcmp(dest, "Ac")==0 || strcmp(dest, "AC")==0 || strcmp(dest, "ac")==0) return "Ac";
    else return NULL;
}

void* heartbeatThread(void* arg) {
    hb_args_t* args = (hb_args_t*)arg;

    char str[BUFFER_SIZE];
    snprintf(str, sizeof(str), "HEARTBEAT from %s", args->campus);

    while (1) {
        sendto(args->udp_sock, str, strlen(str), 0, (struct sockaddr*)&args->udp_addr, sizeof(args->udp_addr));
        sleep(10);
    }
    return NULL;
}

int main() {
    int tcp_sock, udp_sock;
    int pc_server_sock, pc_client_sock = -1;

    struct sockaddr_in serv_addr, udp_addr, pc_addr;
    char buffer[BUFFER_SIZE];

    // ---------------------- TCP SOCKET (CENTRAL SERVER) ------------------------
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TCP_PORT);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    connect(tcp_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    printf("[CAMPUS] Connected to Central Server\n");

    // ---------------------- UDP SOCKET (HEARTBEAT) ------------------------
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(UDP_PORT);
    udp_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // ======================= LOGIN =============================
    char campus[20], pass[20];

    printf("Enter campus: ");
    scanf("%s", campus);

    send(tcp_sock, campus, strlen(campus), 0);
    usleep(200000);

    int r = read(tcp_sock, buffer, BUFFER_SIZE);
    buffer[r] = 0;

    if (buffer[0] == '0') {
        printf("[CLIENT] Invalid campus\n");
        return 0;
    }

    // Assign PC listening port based on campus
    if (strcmp(campus, "cfd") == 0) PC_PORT = CAMPUS_PORTS[0];
    else if (strcmp(campus, "lhr") == 0) PC_PORT = CAMPUS_PORTS[1];
    else if (strcmp(campus, "pwr") == 0) PC_PORT = CAMPUS_PORTS[3];
    else if (strcmp(campus, "kar") == 0) PC_PORT = CAMPUS_PORTS[2];
    else if (strcmp(campus, "mlt") == 0) PC_PORT = CAMPUS_PORTS[4];

    printf("%s", buffer);

    printf("Enter password: ");
    scanf("%s", pass);

    send(tcp_sock, pass, strlen(pass), 0);
    usleep(200000);

    r = read(tcp_sock, buffer, BUFFER_SIZE);
    buffer[r] = 0;

    if (buffer[0] == '0') {
        printf("[CLIENT] Invalid password\n");
        return 0;
    }

    printf("%s", buffer);

    // ================= HEARTBEAT THREAD ===================
    pthread_t hbThread;
    hb_args_t args;

    args.udp_sock = udp_sock;
    args.udp_addr = udp_addr;
    strcpy(args.campus, campus);

    pthread_create(&hbThread, NULL, heartbeatThread, &args);

    // ================= PC SERVER TCP SOCKET ====================
    pc_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    memset(&pc_addr, 0, sizeof(pc_addr));

    pc_addr.sin_family = AF_INET;
    pc_addr.sin_addr.s_addr = inet_addr("0.0.0.0");
    pc_addr.sin_port = htons(PC_PORT);

    bind(pc_server_sock, (struct sockaddr*)&pc_addr, sizeof(pc_addr));
    listen(pc_server_sock, 5);

    printf("[CAMPUS] Waiting for PC client on port %d...\n", PC_PORT);

    // ===================== FORWARDING LOOP ======================
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);

        FD_SET(tcp_sock, &fds);

        FD_SET(pc_server_sock, &fds);
        int maxfd = pc_server_sock;

        if (pc_client_sock > 0) {
            FD_SET(pc_client_sock, &fds);
            if (pc_client_sock > maxfd) maxfd = pc_client_sock;
        }

        int a = select(maxfd + 1, &fds, NULL, NULL, NULL);

        // Accept PC client
        if (FD_ISSET(pc_server_sock, &fds)) {
            pc_client_sock = accept(pc_server_sock, NULL, NULL);
            printf("[CAMPUS] PC client connected\n");
        }

        // RECEIVE from PC -> forward to Central Server
        if (pc_client_sock > 0 && FD_ISSET(pc_client_sock, &fds)) {
            memset(buffer, 0, BUFFER_SIZE);
            int n = read(pc_client_sock, buffer, BUFFER_SIZE);
            
            if (n <= 0) {
                printf("[CAMPUS] PC client disconnected\n");
                close(pc_client_sock);
                pc_client_sock = -1;
            } else {
                printf("[PC -> CAMPUS -> SERVER] %s\n", buffer);
                send(tcp_sock, buffer, n, 0);
            }
        }

        // RECEIVE from Central Server -> forward to PC
        if (FD_ISSET(tcp_sock, &fds)) {
            memset(buffer, 0, BUFFER_SIZE);
            int n = read(tcp_sock, buffer, BUFFER_SIZE);

            if (n <= 0) {
                printf("[CAMPUS] Central server disconnected!\n");
                break;
            }

            printf("[SERVER -> CAMPUS -> PC] %s\n", buffer);

            if (pc_client_sock > 0) send(pc_client_sock, buffer, n, 0);
        }
    }

    pthread_cancel(hbThread);
    pthread_join(hbThread, NULL);

    return 0;
}
