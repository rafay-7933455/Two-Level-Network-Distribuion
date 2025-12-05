// CAMPUS_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define CAMPUS_IP "127.0.0.1"
#define BUFFER_SIZE 1024

const int CAMPUS_PORTS[5] = {
  //cfd,  lhr,  kar,  pwr,  mlt
    7000, 7001, 7002, 7003, 7004
};
int CAMPUS_PORT = 7000;
char client_name[20] = {'\0'};

int main() {
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char campus[20];

    printf("Client Name:");
    scanf("%s", client_name);

    printf("Campus Name:");
    scanf("%s", campus);

    // Assign PC listening port based on campus
    if (strcmp(campus, "cfd") == 0) CAMPUS_PORT = CAMPUS_PORTS[0];
    else if (strcmp(campus, "lhr") == 0) CAMPUS_PORT = CAMPUS_PORTS[1];
    else if (strcmp(campus, "pwr") == 0) CAMPUS_PORT = CAMPUS_PORTS[3];
    else if (strcmp(campus, "kar") == 0) CAMPUS_PORT = CAMPUS_PORTS[2];
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

    // ------------- MAIN LOOP --------------
    while (1) {
        printf("PC You: ");
        scanf("%s", buffer);

        char combined[1045];
        snprintf(combined, sizeof(combined), "%s: %s", client_name, buffer);

        // send to campus
        send(sock, combined, strlen(combined), 0);

        if (strcmp(buffer, "exit") == 0)
            break;

        // wait for reply
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(sock, buffer, BUFFER_SIZE - 1);

        if (n <= 0) {
            printf("[PC] Campus disconnected\n");
            break;
        }

        buffer[n] = 0;
        printf("Server: %s\n", buffer);
    }

    close(sock);
    return 0;
}
