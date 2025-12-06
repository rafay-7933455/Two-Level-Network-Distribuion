// pc.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define CAMPUS_IP "127.0.0.1"
#define BUFFER_SIZE 1024

int CAMPUS_PORT = 7000;

const int CAMPUS_PORTS[5] = {7000,7001,7002,7003,7004};

char client_name[20];
char campus[10];
char dept[10];

int sock;

void* receiver_thread(void* arg)
{
    char buffer[BUFFER_SIZE];

    while (1)
    {
        memset(buffer,0,BUFFER_SIZE);
        int n = read(sock, buffer, BUFFER_SIZE-1);

        if (n <= 0)
        {
            printf("\n[PC] campus disconnected\n");
            exit(0);
        }

        buffer[n]=0;

        printf("\n[RECEIVED] %s\n", buffer);
        printf("Target Campus: ");
        fflush(stdout);
    }
}

int main()
{
    struct sockaddr_in serv;

    printf("Client Name: ");
    scanf("%s", client_name);

    printf("Campus Name: ");
    scanf("%s", campus);

    printf("Department Name: ");
    scanf("%s", dept);

    // assign port
    if (strcmp(campus,"cfd")==0) CAMPUS_PORT = 7000;
    if (strcmp(campus,"lhr")==0) CAMPUS_PORT = 7001;
    if (strcmp(campus,"kar")==0) CAMPUS_PORT = 7002;
    if (strcmp(campus,"pwr")==0) CAMPUS_PORT = 7003;
    if (strcmp(campus,"mlt")==0) CAMPUS_PORT = 7004;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    serv.sin_family = AF_INET;
    serv.sin_port = htons(CAMPUS_PORT);
    serv.sin_addr.s_addr = inet_addr(CAMPUS_IP);

    connect(sock, (struct sockaddr*)&serv, sizeof(serv));
    printf("[PC] Connected\n");

    pthread_t recv;
    pthread_create(&recv, NULL, receiver_thread, NULL);

    char buffer[BUFFER_SIZE];

    while (1)
    {
        char tc[10], tn[10], td[10];

        printf("\nTarget Campus: ");
        scanf("%s", tc);

        printf("Target Client: ");
        scanf("%s", tn);

        printf("Target Department: ");
        scanf("%s", td);

        printf("Message: ");
        scanf("%s", buffer);

        char msg[2*BUFFER_SIZE];

        snprintf(msg, sizeof(msg),
                 "From %s:%s:%s To %s:%s:%s -> %s",
                 dept, campus, client_name,
                 td, tc, tn, buffer);

        send(sock, msg, strlen(msg), 0);

        if (strcmp(buffer,"exit")==0)
            break;
    }
    pthread_cancel(recv);
    pthread_join(recv, NULL);
    close(sock);
    return 0;
}
