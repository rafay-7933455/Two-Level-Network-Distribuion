// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define TCP_PORT 8080
#define UDP_PORT 9090
#define BUFFER_SIZE 1024

typedef struct {
    int udp_sock;
    struct sockaddr_in udp_addr;
    char campus[20];
} hb_args_t;

void* heartbeatThread(void* arg) {
    hb_args_t* args = (hb_args_t*)arg;
    int udp_sock = args->udp_sock;
    struct sockaddr_in udp_addr = args->udp_addr;
    
    char str[BUFFER_SIZE];
    snprintf(str, sizeof(str), "HEARTBEAT from %s", args->campus);


    while (1) {
        sendto(udp_sock, str, sizeof(str), 0, (struct sockaddr*)&udp_addr, sizeof(udp_addr));

        // printf("[CLIENT] Heartbeat sent\n");

        sleep(10);  // sleep IS a cancellation point
    }

    return NULL;
}

int main() {
    int tcp_sock, udp_sock;
    struct sockaddr_in serv_addr, udp_addr;
    char buffer[BUFFER_SIZE];

    // ---------------------- TCP SOCKET ------------------------
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TCP_PORT);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    connect(tcp_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    printf("[CLIENT] Connected to server\n");

    // ---------------------- UDP SOCKET ------------------------
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

    read(tcp_sock, buffer, BUFFER_SIZE);
    if (buffer[0] == '0') {
        printf("[CLIENT] Invalid campus\n");
        return 0;
    }
    else printf("%s", buffer);//it does not run this code
    printf("Enter password: ");
    scanf("%s", pass);
    send(tcp_sock, pass, strlen(pass), 0);
    usleep(200000);

    int len = read(tcp_sock, buffer, BUFFER_SIZE);
    buffer[len] = 0;

    if (buffer[0] == '0') {
        printf("[CLIENT] Invalid password\n");
        return 0;
    }

    printf("%s", buffer);

    pthread_t hbThread;

    hb_args_t args;
    args.udp_sock = udp_sock;
    args.udp_addr = udp_addr;
    strcpy(args.campus, campus);

    pthread_create(&hbThread, NULL, heartbeatThread, &args);


    // ===================== MAIN LOOP ===========================
    while (1) {
        // ------- Send UDP heartbeat -------
        // sendto(udp_sock, "heartbeat", 9, 0, (struct sockaddr*)&udp_addr, sizeof(udp_addr));

        // ------- TCP messaging -------
        printf("\nYou: ");
        scanf("%s", buffer);

        send(tcp_sock, buffer, strlen(buffer), 0);

        if(strcmp(buffer, "exit")==0) break;

        memset(buffer, 0, BUFFER_SIZE);
        int n = read(tcp_sock, buffer, BUFFER_SIZE - 1);
        buffer[n] = 0;

        printf("Server: %s\n", buffer);

        sleep(3);
    }
    pthread_cancel(hbThread);
    pthread_join(hbThread, NULL);

    return 0;
}
