// door_udp_client.c
#define _POSIX_C_SOURCE 200809L
#include "door_udp_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BUF_MAX 256

// UDP socket + destination info
static int g_sock = -1;
static struct sockaddr_in g_dest_notif;
static struct sockaddr_in g_dest_hb;
static socklen_t g_dest_len = 0;

// Module identity + reporting settings
static char           g_module_id[16]       = "M?";
static DoorReportMode g_mode                = DOOR_REPORT_NOTIFICATION;
static int            g_heartbeat_period_ms = 1000;

// Previous-state tracking
static bool g_prev_valid    = false;
static bool g_prev_d0_open  = false;
static bool g_prev_d0_locked= false;
static bool g_prev_d1_open  = false;
static bool g_prev_d1_locked= false;

// Heartbeat timer
static long long g_last_heartbeat_ms = 0;

// ---------------- time helper ----------------
static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

// ---------------- UDP send helper ----------------
static void send_line_notif(const char *line)
{
    if (g_sock < 0) return;
    sendto(g_sock, line, strlen(line), 0,
           (struct sockaddr *)&g_dest_notif, g_dest_len);
}

static void send_line_hb(const char *line)
{
    if (g_sock < 0) return;
    sendto(g_sock, line, strlen(line), 0,
           (struct sockaddr *)&g_dest_hb, g_dest_len);
}

// ---------------- Public API ----------------

bool door_udp_init(const char *host_ip, uint16_t port,
                   const char *module_id,
                   DoorReportMode mode,
                   int heartbeat_period_ms)
{
    if (!host_ip || !module_id) {
        fprintf(stderr, "door_udp_init: host_ip or module_id is NULL\n");
        return false;
    }

    snprintf(g_module_id, sizeof(g_module_id), "%s", module_id);
    g_mode = mode;
    g_heartbeat_period_ms = heartbeat_period_ms > 0 ? heartbeat_period_ms : 1000;

    g_prev_valid = false;
    g_last_heartbeat_ms = now_ms();

    // Create UDP socket
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("door_udp: socket");
        return false;
    }


    // Destination addresses (the HUB) - notifications and heartbeats
    memset(&g_dest_notif, 0, sizeof(g_dest_notif));
    g_dest_notif.sin_family = AF_INET;
    g_dest_notif.sin_port   = htons(port);

    memset(&g_dest_hb, 0, sizeof(g_dest_hb));
    g_dest_hb.sin_family = AF_INET;
    g_dest_hb.sin_port   = htons(port);

    if (inet_pton(AF_INET, host_ip, &g_dest_notif.sin_addr) != 1 ||
        inet_pton(AF_INET, host_ip, &g_dest_hb.sin_addr) != 1) {
        perror("door_udp: inet_pton");
        close(s);
        return false;
    }

    g_sock = s;
    g_dest_len = sizeof(g_dest_notif);

    // Optional HELLO message
    char buf[BUF_MAX];
    snprintf(buf, sizeof(buf), "%s HELLO\n", g_module_id);
    // send hello as a notification (default)
    sendto(g_sock, buf, strlen(buf), 0, (struct sockaddr *)&g_dest_notif, g_dest_len);

    return true;
}

bool door_udp_init2(const char *host_ip, uint16_t notif_port, uint16_t hb_port,
                   const char *module_id,
                   DoorReportMode mode,
                   int heartbeat_period_ms)
{
    // Use the old init with a single port if notif_port == hb_port
    if (notif_port == hb_port) {
        return door_udp_init(host_ip, notif_port, module_id, mode, heartbeat_period_ms);
    }

    if (!host_ip || !module_id) {
        fprintf(stderr, "door_udp_init2: host_ip or module_id is NULL\n");
        return false;
    }

    snprintf(g_module_id, sizeof(g_module_id), "%s", module_id);
    g_mode = mode;
    g_heartbeat_period_ms = heartbeat_period_ms > 0 ? heartbeat_period_ms : 1000;

    g_prev_valid = false;
    g_last_heartbeat_ms = now_ms();

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("door_udp: socket");
        return false;
    }

    // Destination addresses
    memset(&g_dest_notif, 0, sizeof(g_dest_notif));
    g_dest_notif.sin_family = AF_INET;
    g_dest_notif.sin_port   = htons(notif_port);

    memset(&g_dest_hb, 0, sizeof(g_dest_hb));
    g_dest_hb.sin_family = AF_INET;
    g_dest_hb.sin_port   = htons(hb_port);

    if (inet_pton(AF_INET, host_ip, &g_dest_notif.sin_addr) != 1 ||
        inet_pton(AF_INET, host_ip, &g_dest_hb.sin_addr) != 1) {
        perror("door_udp: inet_pton");
        close(s);
        return false;
    }

    g_sock = s;
    g_dest_len = sizeof(g_dest_notif);

    char buf[BUF_MAX];
    snprintf(buf, sizeof(buf), "%s HELLO\n", g_module_id);
    sendto(g_sock, buf, strlen(buf), 0, (struct sockaddr *)&g_dest_notif, g_dest_len);

    return true;
}

void door_udp_update(bool d0_open, bool d0_locked,
                     bool d1_open, bool d1_locked)
{
    if (g_sock < 0) return;

    long long t = now_ms();
    char buf[BUF_MAX];

    // First update → send initial heartbeat
    if (!g_prev_valid) {
        g_prev_d0_open   = d0_open;
        g_prev_d0_locked = d0_locked;
        g_prev_d1_open   = d1_open;
        g_prev_d1_locked = d1_locked;

        g_prev_valid = true;
        g_last_heartbeat_ms = t;

        if (g_mode & DOOR_REPORT_HEARTBEAT) {
            snprintf(buf, sizeof(buf),
                     "%s HEARTBEAT D0=%s,%s D1=%s,%s\n",
                     g_module_id,
                     d0_open   ? "OPEN" : "CLOSED",
                     d0_locked ? "LOCKED" : "UNLOCKED",
                     d1_open   ? "OPEN" : "CLOSED",
                     d1_locked ? "LOCKED" : "UNLOCKED");
            send_line_hb(buf);
        }
        return;
    }

    // -------- Notifications (state change only) --------
    if (g_mode & DOOR_REPORT_NOTIFICATION) {
        if (d0_open != g_prev_d0_open) {
            snprintf(buf, sizeof(buf),
                     "%s EVENT D0 DOOR %s\n",
                     g_module_id,
                     d0_open ? "OPEN" : "CLOSED");
            send_line_notif(buf);
        }
        if (d0_locked != g_prev_d0_locked) {
            snprintf(buf, sizeof(buf),
                     "%s EVENT D0 LOCK %s\n",
                     g_module_id,
                     d0_locked ? "LOCKED" : "UNLOCKED");
            send_line_notif(buf);
        }
        if (d1_open != g_prev_d1_open) {
            snprintf(buf, sizeof(buf),
                     "%s EVENT D1 DOOR %s\n",
                     g_module_id,
                     d1_open ? "OPEN" : "CLOSED");
            send_line_notif(buf);
        }
        if (d1_locked != g_prev_d1_locked) {
            snprintf(buf, sizeof(buf),
                     "%s EVENT D1 LOCK %s\n",
                     g_module_id,
                     d1_locked ? "LOCKED" : "UNLOCKED");
            send_line_notif(buf);
        }
    }

    // -------- Periodic heartbeat --------
    if (g_mode & DOOR_REPORT_HEARTBEAT) {
        if (t - g_last_heartbeat_ms >= g_heartbeat_period_ms) {
            snprintf(buf, sizeof(buf),
                     "%s HEARTBEAT D0=%s,%s D1=%s,%s\n",
                     g_module_id,
                     d0_open   ? "OPEN" : "CLOSED",
                     d0_locked ? "LOCKED" : "UNLOCKED",
                     d1_open   ? "OPEN" : "CLOSED",
                     d1_locked ? "LOCKED" : "UNLOCKED");
            send_line_hb(buf);
            g_last_heartbeat_ms = t;
        }
    }

    // -------- Save previous state --------
    g_prev_d0_open   = d0_open;
    g_prev_d0_locked = d0_locked;
    g_prev_d1_open   = d1_open;
    g_prev_d1_locked = d1_locked;
}

void door_udp_close(void)
{
    if (g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }
    g_dest_len = 0;
    g_prev_valid = false;
}