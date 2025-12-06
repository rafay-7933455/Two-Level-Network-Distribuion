// CAMPUS_client.c  (Parallel Send + Receive)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define CAMPUS_IP "127.0.0.1"
#define BUFFER_SIZE 1024

const int CAMPUS_PORTS[5] = {
    7000, 7001, 7002, 7003, 7004
};

int CAMPUS_PORT = 7000;
char client_name[20] = {'\0'};
char campus[10];
char dept[10];

int sock;

void* receiver_thread(void* arg) {
    char buffer[BUFFER_SIZE];

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(sock, buffer, BUFFER_SIZE - 1);
        if (n <= 0) {
            printf("\n[PC] Campus disconnected (receiver)\n");
            exit(0);
        }
        buffer[n] = 0;
        printf("Interrupted\n[RECEIVED] %s\n", buffer);
        printf("Target Campus: ");       // prompt again
        fflush(stdout);
    }

    return NULL;
}

int main() {
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];

    printf("Client Name:");
    scanf("%s", client_name);

    printf("Campus Name:");
    scanf("%s", campus);

    printf("Department Name:");
    scanf("%s", dept);

    // Assign PC port based on campus
    if (strcmp(campus, "cfd") == 0) CAMPUS_PORT = CAMPUS_PORTS[0];
    else if (strcmp(campus, "lhr") == 0) CAMPUS_PORT = CAMPUS_PORTS[1];
    else if (strcmp(campus, "kar") == 0) CAMPUS_PORT = CAMPUS_PORTS[2];
    else if (strcmp(campus, "pwr") == 0) CAMPUS_PORT = CAMPUS_PORTS[3];
    else if (strcmp(campus, "mlt") == 0) CAMPUS_PORT = CAMPUS_PORTS[4];

    // ---------- TCP SOCKET ----------
    sock = socket(AF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(CAMPUS_PORT);
    serv_addr.sin_addr.s_addr = inet_addr(CAMPUS_IP);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("[PC] Connection to campus failed!\n");
        return 1;
    }

    printf("[PC] Connected to Campus Client\n");

    // Start receiver thread
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receiver_thread, NULL);

    // ---------------- MAIN LOOP (sending only) ----------------
    while (1) {
        char target_campus[10], target_client[10], target_dept[10];
        printf("\n/-----------TYPE A NEW MESSAGE-----------/\nTarget Campus: ");
        scanf("%s", target_campus);
        printf("Target Client: ");
        scanf("%s", target_client);
        printf("Target Department: ");
        scanf("%s", target_dept);

        printf("PC You: ");
        scanf("%s", buffer);

        char combined[2 * BUFFER_SIZE];
        snprintf(combined, sizeof(combined), "From %s->%s, %s<-%s, %s->%s: %s", campus, target_campus, target_dept, dept, client_name, target_client, buffer);

        send(sock, combined, strlen(combined), 0);

        if (strcmp(buffer, "exit") == 0) break;
    }
    pthread_cancel(recv_thread);
    pthread_join(recv_thread, NULL);
    close(sock);
    return 0;
}
