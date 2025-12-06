// campus.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h>

#define TCP_PORT 8080
#define UDP_PORT 9090
#define UDP_BC_PORT 9092
#define BUFFER_SIZE 2048

int PC_PORT = 7000;

const int CAMPUS_PORTS[5] = {7000,7001,7002,7003,7004};

typedef struct {
    int udp_sock;
    struct sockaddr_in udp_addr;
    char campus[20];
} thread_args_t;

void* heartbeatThread(void* arg)
{
    thread_args_t* a = (thread_args_t*)arg;

    char s[BUFFER_SIZE];
    snprintf(s, sizeof(s), "HEARTBEAT from %s", a->campus);

    while (1)
    {
        sendto(a->udp_sock, s, strlen(s), 0,
               (struct sockaddr*)&a->udp_addr,
               sizeof(a->udp_addr));
        sleep(10);
    }
}

void* broadcast_rcv_thread(void* arg)
{
    thread_args_t* a = (thread_args_t*)arg;

    char s[BUFFER_SIZE];
    struct sockaddr_in sender;
    socklen_t slen = sizeof(sender);

    while (1)
    {
        memset(s, 0, sizeof(s));
        int n = recvfrom(a->udp_sock, s, sizeof(s) - 1, 0,
                         (struct sockaddr*)&sender, &slen);
        if (n <= 0) {
            // don't exit; keep listening
            usleep(100000);
            continue;
        }
        s[n] = '\0';
        printf("\n[ADMIN BROADCAST from %s:%d] %s\n",
               inet_ntoa(sender.sin_addr), ntohs(sender.sin_port), s);

        // keep interactive prompt sensible for the user
        printf("Target Campus: ");
        fflush(stdout);
    }
    return NULL;
}

int main()
{
    int tcp_sock, udp_sock;
    int pc_server_sock, pc_client_sock = -1;

    struct sockaddr_in serv_addr, udp_addr, pc_addr;

    char buffer[BUFFER_SIZE];

    // ------- TCP to server -------
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TCP_PORT);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    connect(tcp_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    printf("[CAMPUS] Connected to central\n");

    // ------- UDP heartbeat -------
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(UDP_PORT);
    udp_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    //broadcast
    int udp_listen_sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(UDP_BC_PORT);   // same UDP port as admin broadcast
    listen_addr.sin_addr.s_addr = INADDR_ANY; // bind to all interfaces

    if (bind(udp_listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) < 0) {
        perror("udp bind");
    }

    // Login
    char campus[20], pass[20];

    printf("Campus: ");
    scanf("%s", campus);

    send(tcp_sock, campus, strlen(campus), 0);
    usleep(200000);

    int r = read(tcp_sock, buffer, BUFFER_SIZE);
    buffer[r] = 0;

    if (buffer[0] == '0')
    {
        printf("Invalid campus\n");
        return 0;
    }

    // Assign PC port
    for (int i=0;i<5;i++)
        if (strcmp(campus, (char*[]){"cfd","lhr","kar","pwr","mlt"}[i]) == 0) 
            PC_PORT = CAMPUS_PORTS[i];

    printf("Pass: ");
    scanf("%s", pass);

    send(tcp_sock, pass, strlen(pass), 0);
    usleep(200000);

    r = read(tcp_sock, buffer, BUFFER_SIZE);
    buffer[r] = 0;

    printf("%s", buffer);

    // Heartbeat thread
    pthread_t hb, bcast;
    thread_args_t args;

    args.udp_sock = udp_sock;
    args.udp_addr = udp_addr;
    strcpy(args.campus, campus);

    pthread_create(&hb, NULL, heartbeatThread, &args);
    pthread_create(&bcast, NULL, broadcast_rcv_thread, &args);

    // PC server socket
    pc_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    memset(&pc_addr, 0, sizeof(pc_addr));

    pc_addr.sin_family = AF_INET;
    pc_addr.sin_addr.s_addr = INADDR_ANY;
    pc_addr.sin_port = htons(PC_PORT);

    bind(pc_server_sock, (struct sockaddr*)&pc_addr, sizeof(pc_addr));
    listen(pc_server_sock, 5);

    printf("[CAMPUS] Listening PC on %d\n", PC_PORT);

    while (1)
    {
        fd_set fds;
        FD_ZERO(&fds);

        FD_SET(tcp_sock, &fds);
        FD_SET(pc_server_sock, &fds);

        int maxfd = pc_server_sock;

        if (pc_client_sock > 0)
        {
            FD_SET(pc_client_sock, &fds);
            if (pc_client_sock > maxfd)
                maxfd = pc_client_sock;
        }

        select(maxfd + 1, &fds, NULL, NULL, NULL);

        // PC connect
        if (FD_ISSET(pc_server_sock, &fds))
        {
            pc_client_sock = accept(pc_server_sock, NULL, NULL);
            printf("[CAMPUS] PC connected\n");
        }

        // PC → Server
        if (pc_client_sock > 0 && FD_ISSET(pc_client_sock, &fds))
        {
            memset(buffer, 0, BUFFER_SIZE);
            int n = read(pc_client_sock, buffer, BUFFER_SIZE);

            if (n <= 0)
            {
                close(pc_client_sock);
                pc_client_sock = -1;
            }
            else {
                send(tcp_sock, buffer, n, 0);
            }
        }

        // Server → PC
        if (FD_ISSET(tcp_sock, &fds))
        {
            memset(buffer, 0, BUFFER_SIZE);
            int n = read(tcp_sock, buffer, BUFFER_SIZE);

            if (n <= 0)
            {
                printf("[CAMPUS] server down\n");
                break;
            }

            if (pc_client_sock > 0)
                send(pc_client_sock, buffer, n, 0);
        }
    }
    pthread_cancel(hb);
    pthread_join(hb, NULL);
    return 0;
}
