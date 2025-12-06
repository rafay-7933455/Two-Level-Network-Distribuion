// pc.c  (PC client)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define CAMPUS_IP "127.0.0.1"
#define BUFFER_SIZE 1024

const int CAMPUS_PORTS[6] = {7000,7001,7002,7003,7004,7005};
int CAMPUS_PORT = 7000;
char client_name[20] = {'\0'};
char campus[10];
char dept[10];

int sock;

void* recvr_thread(void* arg)
{
    char buf[BUFFER_SIZE];

    while (1)
    {
        memset(buf, 0, sizeof(buf));
        int n = read(sock, buf, sizeof(buf)-1);

        if (n <= 0)
        {
            printf("\n[PC] Disconnected\n");
            exit(0);
        }

        buf[n] = 0;
        printf("\n[RECEIVED] %s\n", buf);
        printf("Target Campus: ");
        fflush(stdout);
    }
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
    else if (strcmp(campus, "isb") == 0) CAMPUS_PORT = CAMPUS_PORTS[5];

    // ---------- TCP SOCKET ----------
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    memset(&serv_addr,0,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(CAMPUS_PORT);
    serv_addr.sin_addr.s_addr = inet_addr(CAMPUS_IP);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("[PC] connect");
        return 1;
    }

    printf("[PC] Connected to Campus Client on port %d\n", CAMPUS_PORT);

    // Start receiver thread
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, recvr_thread, NULL);

    // ---------------- MAIN LOOP (sending only) ----------------
    while (1) {
        char target_campus[10], target_client[20], target_dept[10];
        printf("\n/-----------TYPE A NEW MESSAGE-----------/\nTarget Dept: ");
        scanf("%s", target_dept);
        printf("Target Campus: ");
        scanf("%s", target_campus);
        printf("Target Client: ");
        scanf("%s", target_client);

        printf("PC You (single-word message): ");
        scanf("%s", buffer);

        char combined[2 * BUFFER_SIZE];
        // format as requested, lowercase "to"
        snprintf(combined, sizeof(combined),
                 "From %s:%s:%s to %s:%s:%s",
                 dept, campus, client_name,
                 target_dept, target_campus, target_client);

        // append message text after a space (but central extracts dest from header)
        // If you want message body, you can append: " <msg>"
        // For now we send the header + space + body
        size_t hdr_len = strlen(combined);
        snprintf(combined + hdr_len, sizeof(combined) - hdr_len, " %s", buffer);

        send(sock, combined, strlen(combined), 0);

        if (strcmp(buffer, "exit") == 0) break;
    }

    pthread_cancel(recv_thread);
    pthread_join(recv_thread, NULL);
    close(sock);
    return 0;
}
