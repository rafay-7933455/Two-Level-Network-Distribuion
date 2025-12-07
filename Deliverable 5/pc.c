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

const int CAMPUS_PORTS[6] = {7000,7001,7002,7003,7004,7005};

char client_name[20];
char campus[10];
char dept[10];

int sock;
pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;


void* receiver_thread(void* arg)
{
    char buffer[BUFFER_SIZE];

    while (1)
    {
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(sock, buffer, BUFFER_SIZE - 1);

        if (n <= 0)
        {
            pthread_mutex_lock(&print_lock);
            printf("\n[PC] Campus disconnected\n");
            pthread_mutex_unlock(&print_lock);
            // exit(0);
            return NULL;
        }

        buffer[n] = 0;

        pthread_mutex_lock(&print_lock);

        if(strncmp(buffer, "broadcast: ", 11)==0){
            char* bcast = buffer+11;
            printf("\n\n================ BROADCAST RECEIVED ================\n");
            printf("%s\n", bcast);
            printf("====================================================\n");
        }
        else printf("%s\n", buffer);
        

        printf("\nTarget Campus: ");   // restore input prompt
        fflush(stdout);

        pthread_mutex_unlock(&print_lock);
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
    if (strcmp(campus,"isb")==0) CAMPUS_PORT = 7005;

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
