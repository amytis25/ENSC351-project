// door_tcp_client.c
#define _POSIX_C_SOURCE 200809L
#include "hal/door_tcp_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BUF_MAX 256

static int            g_sock                = -1;
static char           g_module_id[16]       = "M?";
static DoorReportMode g_mode                = DOOR_REPORT_NOTIFICATION;
static int            g_heartbeat_period_ms = 1000;

static bool           g_prev_valid          = false;
static bool           g_prev_d0_open        = false;
static bool           g_prev_d0_locked      = false;
static bool           g_prev_d1_open        = false;
static bool           g_prev_d1_locked      = false;

static long long      g_last_heartbeat_ms   = 0;

// ---------- time helper ----------
static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

// ---------- low-level send helper ----------
static void send_line(const char *line)
{
    if (g_sock < 0) return;
    size_t len = strlen(line);
    (void)send(g_sock, line, len, 0);
}

// ---------- public API ----------

bool door_tcp_init(const char *host_ip, uint16_t port,
                   const char *module_id,
                   DoorReportMode mode,
                   int heartbeat_period_ms)
{
    if (!host_ip || !module_id) {
        fprintf(stderr, "door_tcp_init: host_ip/module_id is NULL\n");
        return false;
    }

    snprintf(g_module_id, sizeof(g_module_id), "%s", module_id);
    g_mode = mode;
    g_heartbeat_period_ms = (heartbeat_period_ms > 0) ? heartbeat_period_ms : 1000;

    g_prev_valid          = false;
    g_last_heartbeat_ms   = now_ms();

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("door_tcp: socket");
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, host_ip, &addr.sin_addr) != 1) {
        perror("door_tcp: inet_pton");
        close(s);
        return false;
    }

    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("door_tcp: connect");
        close(s);
        return false;
    }

    g_sock = s;

    // Optional hello
    char buf[BUF_MAX];
    snprintf(buf, sizeof(buf), "%s HELLO\n", g_module_id);
    send_line(buf);

    return true;
}

void door_tcp_update(bool d0_open, bool d0_locked,
                     bool d1_open, bool d1_locked)
{
    if (g_sock < 0) return;

    long long t = now_ms();
    char buf[BUF_MAX];

    // First-time: initialize previous state and optionally send initial heartbeat
    if (!g_prev_valid) {
        g_prev_d0_open   = d0_open;
        g_prev_d0_locked = d0_locked;
        g_prev_d1_open   = d1_open;
        g_prev_d1_locked = d1_locked;
        g_prev_valid     = true;
        g_last_heartbeat_ms = t;

        if (g_mode & DOOR_REPORT_HEARTBEAT) {
            snprintf(buf, sizeof(buf),
                     "%s HEARTBEAT D0=%s,%s D1=%s,%s\n",
                     g_module_id,
                     d0_open   ? "OPEN"     : "CLOSED",
                     d0_locked ? "LOCKED"   : "UNLOCKED",
                     d1_open   ? "OPEN"     : "CLOSED",
                     d1_locked ? "LOCKED"   : "UNLOCKED");
            send_line(buf);
        }
        return;
    }

    // --- Notification on change ---
    if (g_mode & DOOR_REPORT_NOTIFICATION) {
        // Door 0 position
        if (d0_open != g_prev_d0_open) {
            snprintf(buf, sizeof(buf),
                     "%s EVENT D0 DOOR %s\n",
                     g_module_id,
                     d0_open ? "OPEN" : "CLOSED");
            send_line(buf);
        }
        // Door 0 lock
        if (d0_locked != g_prev_d0_locked) {
            snprintf(buf, sizeof(buf),
                     "%s EVENT D0 LOCK %s\n",
                     g_module_id,
                     d0_locked ? "LOCKED" : "UNLOCKED");
            send_line(buf);
        }
        // Door 1 position
        if (d1_open != g_prev_d1_open) {
            snprintf(buf, sizeof(buf),
                     "%s EVENT D1 DOOR %s\n",
                     g_module_id,
                     d1_open ? "OPEN" : "CLOSED");
            send_line(buf);
        }
        // Door 1 lock
        if (d1_locked != g_prev_d1_locked) {
            snprintf(buf, sizeof(buf),
                     "%s EVENT D1 LOCK %s\n",
                     g_module_id,
                     d1_locked ? "LOCKED" : "UNLOCKED");
            send_line(buf);
        }
    }

    // --- Periodic heartbeat ---
    if (g_mode & DOOR_REPORT_HEARTBEAT) {
        if (t - g_last_heartbeat_ms >= g_heartbeat_period_ms) {
            snprintf(buf, sizeof(buf),
                     "%s HEARTBEAT D0=%s,%s D1=%s,%s\n",
                     g_module_id,
                     d0_open   ? "OPEN"     : "CLOSED",
                     d0_locked ? "LOCKED"   : "UNLOCKED",
                     d1_open   ? "OPEN"     : "CLOSED",
                     d1_locked ? "LOCKED"   : "UNLOCKED");
            send_line(buf);
            g_last_heartbeat_ms = t;
        }
    }

    // Update previous state
    g_prev_d0_open   = d0_open;
    g_prev_d0_locked = d0_locked;
    g_prev_d1_open   = d1_open;
    g_prev_d1_locked = d1_locked;
}

void door_tcp_close(void)
{
    if (g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }
    g_prev_valid = false;
}