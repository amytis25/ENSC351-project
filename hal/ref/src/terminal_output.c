// terminal_output.c
// Sends terminal output to host via listen-only UDP

#define _POSIX_C_SOURCE 200809L
#include "hal/terminal_output.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

static int g_sock = -1;
static struct sockaddr_in g_dst;

static int format_status_text( // The terminal output format as stated in the assignment
    char* out, size_t cap,
    int samples_in_second, int led_hz, double avg_light, int dips,
    const Period_statistics_t* s, const double* hist, int N)
{
    int w = 0, n;
    n = snprintf(out+w, cap-w, "\nSamples: %4d  LED: %3d Hz  avg: %6.3fV  Dips: %3d   ",
                 samples_in_second, led_hz, avg_light, dips);
    if (n < 0) { return 0;}
    w += n;

    if (s) {
        n = snprintf(out+w, cap-w, "Smpl ms[%6.1f, %6.1f] avg %6.1f/%4d\n",
                     s->minPeriodInMs, s->maxPeriodInMs, s->avgPeriodInMs, s->numSamples);
          if (n < 0) { return 0;}
          w += n;
    } else {
        n = snprintf(out+w, cap-w, "\n"); 
          if (n < 0) { return 0;}
          w += n;
    }

    if (hist && N > 0) {
        int to_show = (N < 10) ? N : 10;
        for (int k = 0; k < to_show; k++) {
            int idx = (N <= 10) ? k : (int)((double)k * (double)(N-1) / 9.0 + 0.5);
            n = snprintf(out+w, cap-w, " %4d:%6.3f", idx, hist[idx]);
            if (n < 0) { return 0;}
            w += n;
            if ((size_t)w >= cap - 16) break;
        }
        n = snprintf(out+w, cap-w, "\n"); 
        if (n < 0) { return 0;}
        w += n;
    }
    return w;
}

bool listener_udp_init(const char* host_ip, int port)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket"); return false; }

    memset(&g_dst, 0, sizeof(g_dst));
    g_dst.sin_family = AF_INET;
    g_dst.sin_port   = htons(port);
    if (inet_pton(AF_INET, host_ip, &g_dst.sin_addr) != 1) {
        perror("inet_pton");
        close(s);
        return false;
    }
    g_sock = s;                 
    return true;
}

void udp_send_status(
    int samples_in_second, int led_hz, double avg_light, int dips,
    const Period_statistics_t *s, const double *hist, int N)
{
    if (g_sock < 0) return;
    char msg[2048];
    int len = format_status_text(msg, sizeof(msg),
                                 samples_in_second, led_hz, avg_light, dips,
                                 s, hist, N);
    if (len <= 0) return;
    (void)sendto(g_sock, msg, (size_t)len, 0, (struct sockaddr*)&g_dst, sizeof(g_dst));
}

void listener_socket_close(void)
{
    if (g_sock >= 0) close(g_sock);
    g_sock = -1;
}