// door_udp.c
#define _POSIX_C_SOURCE 200809L
#include "hal/door_udp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include "hal/timing.h"

#define BUF_MAX 256

// UDP socket + destination info
static int g_sock = -1;
static struct sockaddr_in g_dest_notif;
static struct sockaddr_in g_dest_hb;
static socklen_t g_dest_len = 0;
// Command listener
static pthread_t g_cmd_thread;
static volatile int g_cmd_running = 0;
static uint16_t g_bound_notif_port = 0;

// Forward declarations for helpers used before their definitions
static void send_line_notif(const char *line);
static void send_line_hb(const char *line);

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

/* Registered command handler (set by the app layer) */
static DoorCmdHandler g_cmd_handler = NULL;
static void *g_cmd_handler_ctx = NULL;

bool door_udp_register_command_handler(DoorCmdHandler handler, void *ctx)
{
    g_cmd_handler = handler;
    g_cmd_handler_ctx = ctx;
    return true;
}

bool door_udp_send_feedback(const char *module, int cmdid, const char *target, const char *action)
{
    if (g_sock < 0) return false;
    char out[BUF_MAX];
    snprintf(out, sizeof(out), "%s FEEDBACK %d %s %s\n", module, cmdid, target, action);
    send_line_notif(out);
    return true;
}

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

static void *door_cmd_thread(void *arg)
{
    (void)arg;
    char buf[BUF_MAX];
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);

    while (g_cmd_running) {
        ssize_t n = recvfrom(g_sock, buf, sizeof(buf)-1, 0,
                             (struct sockaddr *)&src, &srclen);
        if (n < 0) {
            if (errno == EINTR) continue;
            /* Timeout or no data; re-check the running flag to exit cleanly. */
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            sleepForMs(1);
            continue;
        }
        buf[n] = '\0';

        // parse: <MODULE> COMMAND <CMDID> <TARGET> <ACTION>
        char *save = NULL;
        char *mod = strtok_r(buf, " \t\r\n", &save);
        if (!mod) continue;
        char *type = strtok_r(NULL, " \t\r\n", &save);
        if (!type) continue;
        if (strcmp(type, "COMMAND") != 0) continue;
        char *cmdid_s = strtok_r(NULL, " \t\r\n", &save);
        char *target = strtok_r(NULL, " \t\r\n", &save);
        char *action = strtok_r(NULL, " \t\r\n", &save);
        int cmdid = 0;
        if (cmdid_s) cmdid = atoi(cmdid_s);
        if (!target || !action) continue;

        // Only act on commands addressed to this module
        if (strcmp(mod, g_module_id) == 0) {
            if (g_cmd_handler) {
                g_cmd_handler(mod, cmdid, target, action, g_cmd_handler_ctx);
            } else {
                // No handler registered: keep legacy behavior and send basic FEEDBACK
                char out[BUF_MAX];
                snprintf(out, sizeof(out), "%s FEEDBACK %d %s %s\n", g_module_id, cmdid, target, action);
                send_line_notif(out);
            }
        }
    }
    return NULL;
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

    /* Make recvfrom() time out periodically so the command listener
     * thread can observe `g_cmd_running` and exit cleanly on shutdown. */
    struct timeval tv2;
    tv2.tv_sec = 1; tv2.tv_usec = 0; /* 1s */
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv2, sizeof(tv2));

    /* Make recvfrom() time out periodically so the command listener
     * thread can observe `g_cmd_running` and exit cleanly on shutdown. */
    struct timeval tv;
    tv.tv_sec = 1; tv.tv_usec = 0; /* 1s */
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

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

    // Bind local socket to notif_port so it can receive commands
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(notif_port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("door_udp: bind notif_port");
        close(s);
        return false;
    }

    g_sock = s;
    g_dest_len = sizeof(g_dest_notif);
    g_bound_notif_port = notif_port;

    char buf[BUF_MAX];
    snprintf(buf, sizeof(buf), "%s HELLO\n", g_module_id);
    sendto(g_sock, buf, strlen(buf), 0, (struct sockaddr *)&g_dest_notif, g_dest_len);

    // Start command listener
    g_cmd_running = 1;
    if (pthread_create(&g_cmd_thread, NULL, door_cmd_thread, NULL) != 0) {
        g_cmd_running = 0;
    }

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
            /* Keep backwards-compatible comma-separated states so the hub's
             * parser (which expects "D0=OPEN,LOCKED") continues to work.
             * Map D0 -> door sensor, D1 -> lock state. For a single-door
             * module we populate both tokens with the same logical door
             * + lock pair so existing consumers see both values. */
            snprintf(buf, sizeof(buf),
                     "%s HEARTBEAT D0=%s,%s D1=%s,%s\n",
                     g_module_id,
                     d0_open   ? "OPEN" : "CLOSED",
                     d1_locked ? "LOCKED" : "UNLOCKED",
                     d0_open   ? "OPEN" : "CLOSED",
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
        /* D0 is sensor-only (door state). D1 is lock-only (lock state).
         * Only emit D0 DOOR events and D1 LOCK events. */
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
            /* Emit combined D0/D1 tokens for compatibility. Use d0_open
             * for the door state and d1_locked for the lock state. */
            snprintf(buf, sizeof(buf),
                     "%s HEARTBEAT D0=%s,%s D1=%s,%s\n",
                     g_module_id,
                     d0_open   ? "OPEN" : "CLOSED",
                     d1_locked ? "LOCKED" : "UNLOCKED",
                     d0_open   ? "OPEN" : "CLOSED",
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
    // Stop command listener first so the thread can exit on its recv timeout.
    if (g_cmd_running) {
        g_cmd_running = 0;
        pthread_join(g_cmd_thread, NULL);
    }

    if (g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }

    g_dest_len = 0;
    g_prev_valid = false;
}
