// campus.c
// Campus process: connects to server via TCP, sends UDP heartbeats, accepts up to 10 PC clients.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"
#define TCP_PORT 8080
#define UDP_PORT 9090
#define BUFFER_SIZE 2048
#define MAX_PC 10

// PC listening base ports per campus (cfd..isb)
const int CAMPUS_PORTS[6] = {7000,7001,7002,7003,7004,7005};
const char* CAMPUS_NAMES[6] = {"cfd","lhr","kar","pwr","mlt","isb"};

int PC_PORT = 7000;

int tcp_sock = -1;
int pc_server_sock = -1;
int pc_client_socks[MAX_PC];
pthread_mutex_t pc_lock = PTHREAD_MUTEX_INITIALIZER;

char campus[20];
char password[64];

void* heartbeatThread(void* arg)
{
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) { perror("heartbeat udp socket"); return NULL; }

    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    servaddr.sin_port = htons(UDP_PORT);

    char s[BUFFER_SIZE];
    snprintf(s, sizeof(s), "HEARTBEAT from %s", campus);

    while (1)
    {
        sendto(udp_sock, s, strlen(s), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
        sleep(5);
    }
    return NULL;
}

void forward_to_all_pcs(const char* msg, int len)
{
    pthread_mutex_lock(&pc_lock);
    for (int i=0;i<MAX_PC;i++)
    {
        if (pc_client_socks[i] > 0)
        {
            send(pc_client_socks[i], msg, len, 0);
        }
    }
    pthread_mutex_unlock(&pc_lock);
}

void* server_receiver_thread(void* arg)
{
    char buffer[BUFFER_SIZE];
    while (1)
    {
        memset(buffer,0,sizeof(buffer));
        int n = read(tcp_sock, buffer, BUFFER_SIZE-1);
        if (n <= 0)
        {
            printf("\n[CAMPUS %s] Server disconnected\n", campus);
            exit(0);
        }
        buffer[n]=0;

        // If it's a broadcast message, display and forward; else forward normal messages to PCs
        if (strncmp(buffer, "broadcast: ", 11) == 0)
        {
            printf("\n\n================ BROADCAST RECEIVED ================\n");
            printf("%s\n", buffer+11);
            printf("====================================================\n");
        }

        // Forward everything to all PCs (including broadcasts)
        forward_to_all_pcs(buffer, n);
    }
    return NULL;
}

void* pc_handler_thread(void* arg)
{
    int idx = *((int*)arg);
    free(arg);
    int csock = pc_client_socks[idx];
    char buffer[BUFFER_SIZE];

    while (1)
    {
        memset(buffer,0,sizeof(buffer));
        int n = read(csock, buffer, BUFFER_SIZE-1);
        if (n <= 0)
        {
            close(csock);
            pthread_mutex_lock(&pc_lock);
            pc_client_socks[idx] = -1;
            pthread_mutex_unlock(&pc_lock);
            printf("[CAMPUS %s] PC client %d disconnected\n", campus, idx);
            return NULL;
        }

        // forward to server via tcp
        send(tcp_sock, buffer, n, 0);
    }
    return NULL;
}

int find_campus_index(const char* name)
{
    for (int i=0;i<6;i++) if (strcmp(name, CAMPUS_NAMES[i])==0) return i;
    return -1;
}

int main()
{
    for (int i=0;i<MAX_PC;i++) pc_client_socks[i] = -1;

    printf("Campus name (cfd/lhr/kar/pwr/mlt/isb): ");
    scanf("%s", campus);

    int idx = find_campus_index(campus);
    if (idx==-1) { fprintf(stderr,"Invalid campus name\n"); return 1; }
    PC_PORT = CAMPUS_PORTS[idx];

    // connect to server tcp
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock < 0) { perror("tcp socket"); exit(1); }

    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(TCP_PORT);
    servaddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(tcp_sock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("[CAMPUS] Connect failed");
        exit(1);
    }
    printf("[CAMPUS %s] Connected to central server\n", campus);

    // LOGIN: send campus name
    send(tcp_sock, campus, strlen(campus), 0);

    usleep(150000);
    char buffer[BUFFER_SIZE];
    int r = read(tcp_sock, buffer, BUFFER_SIZE-1);
    if (r <= 0) { fprintf(stderr,"Login response read failed\n"); return 1; }
    buffer[r]=0;
    if (buffer[0]=='0')
    {
        printf("Login refused (already logged in or invalid). Exiting.\n");
        return 0;
    }

    // send password
    printf("Password: ");
    scanf("%s", password);
    send(tcp_sock, password, strlen(password), 0);

    usleep(150000);
    r = read(tcp_sock, buffer, BUFFER_SIZE-1);
    if (r <= 0) { fprintf(stderr,"Login ack read failed\n"); return 1; }
    buffer[r]=0;
    printf("%s", buffer);
    if (strncmp(buffer,"Login Successful",15)!=0) { printf("Login failed\n"); return 0; }

    // start heartbeat thread
    pthread_t hb_thread;
    pthread_create(&hb_thread, NULL, heartbeatThread, NULL);

    // start server receiver thread
    pthread_t srv_recv;
    pthread_create(&srv_recv, NULL, server_receiver_thread, NULL);

    // setup server socket for PC clients
    pc_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (pc_server_sock < 0) { perror("pc server socket"); exit(1); }

    int opt=1;
    setsockopt(pc_server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in pc_addr;
    memset(&pc_addr,0,sizeof(pc_addr));
    pc_addr.sin_family = AF_INET;
    pc_addr.sin_port = htons(PC_PORT);
    pc_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(pc_server_sock, (struct sockaddr*)&pc_addr, sizeof(pc_addr)) < 0)
    {
        perror("[CAMPUS] bind pc server");
        exit(1);
    }
    if (listen(pc_server_sock, MAX_PC) < 0) { perror("listen pc"); exit(1); }

    printf("[CAMPUS %s] Listening for PC clients on port %d\n", campus, PC_PORT);

    // main loop: accept PC clients and also forward server messages handled by server_receiver_thread
    while (1)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(pc_server_sock, &fds);
        int maxfd = pc_server_sock;

        pthread_mutex_lock(&pc_lock);
        for (int i=0;i<MAX_PC;i++)
        {
            if (pc_client_socks[i] > 0)
            {
                FD_SET(pc_client_socks[i], &fds);
                if (pc_client_socks[i] > maxfd) maxfd = pc_client_socks[i];
            }
        }
        pthread_mutex_unlock(&pc_lock);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int sel = select(maxfd+1, &fds, NULL, NULL, &tv);
        if (sel < 0 && errno!=EINTR) { perror("select"); }

        if (FD_ISSET(pc_server_sock, &fds))
        {
            int newpc = accept(pc_server_sock, NULL, NULL);
            if (newpc >= 0)
            {
                pthread_mutex_lock(&pc_lock);
                int placed = 0;
                for (int i=0;i<MAX_PC;i++)
                {
                    if (pc_client_socks[i] == -1)
                    {
                        pc_client_socks[i] = newpc;
                        placed = 1;
                        // spawn handler
                        int *arg = malloc(sizeof(int));
                        *arg = i;
                        pthread_t t;
                        pthread_create(&t, NULL, pc_handler_thread, arg);
                        printf("[CAMPUS %s] PC connected (slot %d)\n", campus, i);
                        break;
                    }
                }
                pthread_mutex_unlock(&pc_lock);
                if (!placed)
                {
                    const char *msg = "Campus PC slots full\n";
                    send(newpc, msg, strlen(msg), 0);
                    close(newpc);
                }
            }
        }

        // also check existing PC sockets for activity (their handlers will pick up/read) - we don't need to do anything here
        // loop back
    }

    return 0;
}
