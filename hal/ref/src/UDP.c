// UDP.c 
// Creates UDP command server for light_sampler program.
// help, ?, count, length, dips, history, stop, <enter>.
// Prints error message with unknown commands. 

#define _GNU_SOURCE
#include "hal/UDP.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ctype.h>
#include <netinet/in.h> 

#define UDP_PKT_MAX 1400

static int g_sock = -1;
static pthread_t g_thread;
static volatile bool g_running = false;
static UdpCallbacks g_cb = {0};
static char g_last_cmd[64] = {0};

static void send_text(int sock, const struct sockaddr_in* cli, const char* fmt, ...)
{
    if (sock < 0 || !cli || !g_running) return;

    char buf[UDP_PKT_MAX];
    va_list ap; 
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(buf)) n = (int)sizeof(buf);

    (void)sendto(sock, buf, (size_t)n, 0, (const struct sockaddr*)cli, sizeof(*cli));
}

static void send_help(int sock, const struct sockaddr_in* cli)
{
    const char* msg =
        "Accepted commands:\n"
        "help / ?        - show this list\n"
        "count           - total samples taken\n"
        "length          - samples taken in previous second\n"
        "dips            - dips detected in previous second\n"
        "history         - voltage samples (3 decimals, 10 per line)\n"
        "stop            - terminate program\n"
        "<enter>         - repeat previous command\n";
    send_text(sock, cli, "%s", msg);
}

// Send all voltage samples (10 per line, multi-packet safe)
static void send_history(int sock, const struct sockaddr_in* cli, const double* hist, int N)
{
    char pkt[UDP_PKT_MAX];
    int pos = 0, on_line = 0;

    for (int i = 0; i < N; i++) {
        char one[32];
        int len = snprintf(one, sizeof(one), "%.3f%s",
                           hist[i],
                           (on_line == 9 || i == N - 1) ? "\n" : ", ");
        if (len < 0) len = 0;

        if (pos + len >= (int)sizeof(pkt)) {
            sendto(sock, pkt, pos, 0, (const struct sockaddr*)cli, sizeof(*cli));
            pos = 0;
        }
        memcpy(pkt + pos, one, (size_t)len);
        pos += len;
        on_line = (on_line + 1) % 10;
    }
    if (pos > 0)
        sendto(sock, pkt, pos, 0, (const struct sockaddr*)cli, sizeof(*cli));
}

static void* udp_thread(void* arg)
{
    (void)arg;
    struct sockaddr_in cli;
    socklen_t slen;
    char buf[2048];

    while (g_running) {
        slen = sizeof(cli);
        ssize_t n = recvfrom(g_sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr*)&cli, &slen);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (g_running) perror("recvfrom");
            break;
        }
        buf[n] = '\0';

        // Trim whitespace
        char* s = buf;
        while (*s && isspace((unsigned char)*s)) s++;
        for (ssize_t i = (ssize_t)strlen(s) - 1; i >= 0 &&
             isspace((unsigned char)s[i]); --i) s[i] = '\0';

        // Blank line = repeat last command
        if (s[0] == '\0') {
            if (g_last_cmd[0] == '\0') {
                send_text(g_sock, &cli, "Unknown command (no previous).\n");
                continue;
            }
            s = g_last_cmd;
        } else {
            size_t L = strlen(s);
            if (L >= sizeof(g_last_cmd)) L = sizeof(g_last_cmd) - 1;
            for (size_t i = 0; i < L; i++)
                g_last_cmd[i] = (char)tolower((unsigned char)s[i]);
            g_last_cmd[L] = '\0';
            s = g_last_cmd;
        }

        if (!strcmp(s, "help") || !strcmp(s, "?")) {
            send_help(g_sock, &cli);

        } else if (!strcmp(s, "count")) {
            long long c = g_cb.get_count ? g_cb.get_count() : 0;
            send_text(g_sock, &cli, "# samples taken total: %lld\n", c);

        } else if (!strcmp(s, "length")) {
            int L = g_cb.get_history_size ? g_cb.get_history_size() : 0;
            send_text(g_sock, &cli, "# samples taken last second: %d\n", L);

        } else if (!strcmp(s, "dips")) {
            int d = g_cb.get_dips ? g_cb.get_dips() : 0;
            send_text(g_sock, &cli, "# Dips: %d\n", d);

        } else if (!strcmp(s, "history")) {
            int N = 0;
            double* H = g_cb.get_history ? g_cb.get_history(&N) : NULL;
            if (!H || N <= 0) {
                send_text(g_sock, &cli, "(no history)\n");
            } else {
                send_history(g_sock, &cli, H, N);
                free(H);
            }

        } else if (!strcmp(s, "stop")) {
            if (g_cb.request_shutdown) g_cb.request_shutdown(); // tell main loop
            g_running = false;
            send_text(g_sock, &cli, "Program terminating.\n");
            break;

        } else {
            send_text(g_sock, &cli, "Unknown command: %s\n", s);
        }
    }

    return NULL;
}

int udp_start(uint16_t port, UdpCallbacks cb)
{
    if (g_running) return 0;
    g_cb = cb;

    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) { perror("socket"); return -1; }

    int yes = 1;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in srv = {0};
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = htonl(INADDR_ANY);
    srv.sin_port = htons(port);
    if (bind(g_sock, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("bind");
        close(g_sock);
        g_sock = -1;
        return -1;
    }

    g_running = true;
    if (pthread_create(&g_thread, NULL, udp_thread, NULL) != 0) {
        perror("pthread_create");
        close(g_sock);
        g_sock = -1;
        g_running = false;
        return -1;
    }
    return 0;
}

void udp_stop(void)
{
    if (!g_running && g_sock < 0) return;     // small guard
    g_running = false;
    if (g_sock >= 0) {
        shutdown(g_sock, SHUT_RDWR);
        pthread_join(g_thread, NULL);
        close(g_sock);
        g_sock = -1;
    }
}