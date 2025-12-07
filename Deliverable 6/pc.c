// pc.c
// Simple PC client: connect to campus TCP port, send/receive messages.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 2048
#define CAMPUS_IP "127.0.0.1"

int sockfd = -1;
pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;
char client_name[64];
char campus[16];
char dept[16];

void* receiver_thread(void* arg)
{
    char buffer[BUFFER_SIZE];
    while (1)
    {
        memset(buffer,0,sizeof(buffer));
        int n = read(sockfd, buffer, BUFFER_SIZE-1);
        if (n <= 0)
        {
            pthread_mutex_lock(&print_lock);
            printf("\n[PC] Campus disconnected\n");
            pthread_mutex_unlock(&print_lock);
            return NULL;
        }
        buffer[n]=0;
        pthread_mutex_lock(&print_lock);
        printf("\n[RECV] %s\n", buffer);
        printf("\nTarget Department: ");
        fflush(stdout);
        pthread_mutex_unlock(&print_lock);
    }
    return NULL;
}

int main()
{
    int port;
    printf("Client Name: ");
    scanf("%s", client_name);
    printf("Campus Name (cfd/lhr/kar/pwr/mlt/isb): ");
    scanf("%s", campus);
    printf("Department: ");
    scanf("%s", dept);
    printf("Campus Port (e.g., 7000..7005): ");
    scanf("%d", &port);

    struct sockaddr_in serv;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    serv.sin_addr.s_addr = inet_addr(CAMPUS_IP);

    if (connect(sockfd, (struct sockaddr*)&serv, sizeof(serv)) < 0)
    {
        perror("connect to campus failed");
        return 1;
    }
    printf("[PC] Connected to campus on port %d\n", port);

    pthread_t recv;
    pthread_create(&recv, NULL, receiver_thread, NULL);

    char tc[16], tn[16], td[16];
    char msgtext[1024];

    while (1)
    {
        printf("\nTarget Department: ");
        scanf("%s", tc);
        printf("Target Client: ");
        scanf("%s", tn);
        printf("Target Campus: ");
        scanf("%s", td);
        printf("Message: ");
        scanf("%s", msgtext);

        char msg[BUFFER_SIZE];
        snprintf(msg, sizeof(msg), "From %s:%s:%s To %s:%s:%s -> %s", campus, dept, client_name, td, tc, tn, msgtext);

        send(sockfd, msg, strlen(msg), 0);

        if (strcmp(msgtext,"exit")==0)
            break;
    }

    close(sockfd);
    pthread_cancel(recv);
    pthread_join(recv, NULL);
    return 0;
}
