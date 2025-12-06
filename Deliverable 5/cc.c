// ===============================
// campus.c  (FULL WORKING VERSION)
// ===============================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h>
#include <errno.h>

#define TCP_PORT 8080
#define UDP_HB_PORT 9090
#define BUFFER_SIZE 2048
#define MAX_PC 50

// ------------------------------------
// Global PC client list
// ------------------------------------
int pc_clients[MAX_PC];
int pc_count = 0;

// ------------------------------------
// Campus mapping (MUST MATCH SERVER)
// ------------------------------------
const char *campus_names[6] =
    {"cfd", "lhr", "kar", "pwr", "mlt", "isb"};

const int CAMPUS_PC_PORTS[6] =
    {7000, 7001, 7002, 7003, 7004, 7005};

const int CAMPUS_ADMIN_PORTS[6] =
    {9100, 9101, 9102, 9103, 9104, 9105};

// ------------------------------------
// Heartbeat thread arguments
// ------------------------------------
typedef struct {
    int sock;
    struct sockaddr_in server;
    char campus[20];
} hb_args_t;

// ------------------------------------
// Admin broadcast receive thread args
// ------------------------------------
typedef struct {
    int sock;
} admin_args_t;

// ====================================
// HEARTBEAT THREAD
// ====================================
void *heartbeatThread(void *arg)
{
    hb_args_t *a = (hb_args_t *)arg;
    char msg[256];

    while (1) {
        snprintf(msg, sizeof(msg), "HEARTBEAT from %s", a->campus);

        sendto(a->sock, msg, strlen(msg), 0,
               (struct sockaddr *)&a->server,
               sizeof(a->server));

        sleep(10);
    }

    return NULL;
}

// ====================================
// ADMIN BROADCAST THREAD (UDP)
// ====================================
void *adminThread(void *arg)
{
    admin_args_t *a = (admin_args_t *)arg;

    char buf[BUFFER_SIZE];
    struct sockaddr_in sender;
    socklen_t sl = sizeof(sender);

    while (1)
    {
        memset(buf, 0, sizeof(buf));

        int n = recvfrom(a->sock, buf, sizeof(buf)-1, 0,
                         (struct sockaddr *)&sender, &sl);

        if (n <= 0) {
            usleep(100000);
            continue;
        }

        buf[n] = '\0';
        printf("\n[ADMIN BROADCAST]\n%s\n", buf);

        // Forward to ALL connected PCs
        for (int i = 0; i < pc_count; i++)
        {
            int s = pc_clients[i];
            if (s > 0)
            {
                ssize_t sent = send(s, buf, strlen(buf), 0);
                if (sent < 0)
                    perror("send to PC failed");
            }
        }

    }

    return NULL;
}

// ====================================
// MAIN
// ====================================
int main()
{
    int tcp_sock, udp_hb_sock, udp_admin_sock, pc_server;

    struct sockaddr_in central_addr, hb_addr, bind_addr;

    char buffer[BUFFER_SIZE];

    memset(pc_clients, 0, sizeof(pc_clients));

    // ------------------------------------
    // CONNECT TO CENTRAL SERVER (TCP)
    // ------------------------------------
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);

    memset(&central_addr, 0, sizeof(central_addr));
    central_addr.sin_family = AF_INET;
    central_addr.sin_port = htons(TCP_PORT);
    central_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(tcp_sock, (struct sockaddr *)&central_addr,
                sizeof(central_addr)) < 0) {
        perror("connect central");
        return 1;
    }

    printf("[CAMPUS] Connected to central server\n");

    // ------------------------------------
    // HEARTBEAT UDP SOCKET
    // ------------------------------------
    udp_hb_sock = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&hb_addr, 0, sizeof(hb_addr));
    hb_addr.sin_family = AF_INET;
    hb_addr.sin_port = htons(UDP_HB_PORT);
    hb_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // ------------------------------------
    // ADMIN BROADCAST UDP SOCKET
    // ------------------------------------
    udp_admin_sock = socket(AF_INET, SOCK_DGRAM, 0);

    // ------------------------------------
    // LOGIN (campus + password)
    // ------------------------------------
    char campus[20], pass[20];

    printf("Campus: ");
    scanf("%s", campus);
    send(tcp_sock, campus, strlen(campus), 0);

    sleep(1);

    int r = read(tcp_sock, buffer, sizeof(buffer));
    buffer[r] = 0;

    if (buffer[0] == '0') {
        printf("Invalid campus\n");
        return 0;
    }

    printf("Password: ");
    scanf("%s", pass);
    send(tcp_sock, pass, strlen(pass), 0);

    sleep(1);
    r = read(tcp_sock, buffer, sizeof(buffer));
    buffer[r] = 0;

    printf("%s\n", buffer);

    // ------------------------------------
    // DETERMINE CAMPUS INDEX
    // ------------------------------------
    int idx = -1;
    for (int i = 0; i < 6; i++)
    {
        if (strcmp(campus, campus_names[i]) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        printf("Unknown campus\n");
        return 1;
    }

    int pc_port = CAMPUS_PC_PORTS[idx];
    int admin_port = CAMPUS_ADMIN_PORTS[idx];

    // ------------------------------------
    // BIND admin broadcast socket
    // ------------------------------------
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(admin_port);

    bind(udp_admin_sock, (struct sockaddr *)&bind_addr,
         sizeof(bind_addr));

    printf("[CAMPUS] Admin broadcast bound on %d\n", admin_port);

    // ------------------------------------
    // START THREADS
    // ------------------------------------
    pthread_t hb_thread, admin_thread;

    hb_args_t hb;
    hb.sock = udp_hb_sock;
    hb.server = hb_addr;
    strcpy(hb.campus, campus);

    pthread_create(&hb_thread, NULL, heartbeatThread, &hb);

    admin_args_t ad;
    ad.sock = udp_admin_sock;

    pthread_create(&admin_thread, NULL, adminThread, &ad);

    // ------------------------------------
    // PC SERVER SOCKET (TCP)
    // ------------------------------------
    pc_server = socket(AF_INET, SOCK_STREAM, 0);

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(pc_port);

    bind(pc_server, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    listen(pc_server, 10);

    printf("[CAMPUS] Listening for PCs on %d\n", pc_port);

    // ------------------------------------
    // MAIN SELECT LOOP
    // ------------------------------------
    while (1)
    {
        fd_set fds;
        FD_ZERO(&fds);

        FD_SET(tcp_sock, &fds);
        FD_SET(pc_server, &fds);

        int maxfd = tcp_sock;

        // add PC clients
        for (int i = 0; i < pc_count; i++)
        {
            if (pc_clients[i] > 0) {
                FD_SET(pc_clients[i], &fds);
                if (pc_clients[i] > maxfd)
                    maxfd = pc_clients[i];
            }
        }

        int sel = select(maxfd + 1, &fds, NULL, NULL, NULL);
        if (sel < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // --------------------------------
        // NEW PC CONNECTION
        // --------------------------------
        if (FD_ISSET(pc_server, &fds))
        {
            int cs = accept(pc_server, NULL, NULL);

            if (cs > 0 && pc_count < MAX_PC)
            {
                pc_clients[pc_count++] = cs;
                printf("[CAMPUS] PC connected (%d)\n", pc_count);
            }
        }

        // --------------------------------
        // PC → CENTRAL
        // --------------------------------
        for (int i = 0; i < pc_count; i++)
        {
            int cfd = pc_clients[i];

            if (cfd > 0 && FD_ISSET(cfd, &fds))
            {
                memset(buffer, 0, sizeof(buffer));

                int n = read(cfd, buffer, sizeof(buffer)-1);

                if (n <= 0)
                {
                    close(cfd);
                    pc_clients[i] = 0;
                    continue;
                }
                printf("\n%s\n", buffer);
                send(tcp_sock, buffer, n, 0);
            }
        }

        // --------------------------------
        // CENTRAL → PCs
        // --------------------------------
        if (FD_ISSET(tcp_sock, &fds))
        {
            memset(buffer, 0, sizeof(buffer));
            int n = read(tcp_sock, buffer, sizeof(buffer)-1);

            if (n <= 0) {
                printf("[CAMPUS] Central disconnected\n");
                break;
            }

            for (int i = 0; i < pc_count; i++)
            {
                if (pc_clients[i] > 0)
                    send(pc_clients[i], buffer, n, 0);
            }
        }
    }

    return 0;
}
