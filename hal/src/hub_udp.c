// hub_udp.c
#define _POSIX_C_SOURCE 200809L
#include "hal/hub_udp.h"
#include "hal/timing.h"
#include <curl/curl.h>
#include <arpa/inet.h>
#include "hal/led.h"
#include "hal/led_worker.h"
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "discord_alert.h"

#define HUB_OFFLINE_TIMEOUT_MS 10000  // 10 seconds without heartbeat = offline
#define HUB_MAX_MODULES 16           // max distinct door modules to track

// ---------- Endpoint table (door module -> last known IP:port) ----------

typedef struct {
    char module_id[16];
    struct sockaddr_in addr;
    bool has_addr;
} HubEndpoint;

static HubEndpoint g_endpoints[HUB_MAX_MODULES];
static int g_num_endpoints = 0;

// ---------- Hub UDP sockets / globals ----------

static int          g_sock        = -1;
static int          g_sock2       = -1;
static pthread_t    g_thread_id;
static int          g_listen_port = 0;
static volatile int g_stopping    = 0;
static char         g_webhook_url[512] =
    "https://discord.com/api/webhooks/1445277245743697940/"
    "-DWPsZbIoDTyo1iaXRW3Vo4URqJ1RpkjGQ4ijXENNeYcM9bNHUj90aunxeSU5GsnoZ_M";

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_feedback_cond = PTHREAD_COND_INITIALIZER;
static int             g_next_cmdid = 1;

// Per-door status
static HubDoorStatus g_doors[HUB_MAX_DOORS];

// History ring buffer
static HubEvent g_history[HUB_MAX_HISTORY];
static int      g_hist_head = 0; // next slot to write
static int      g_hist_count = 0;

// Track pending commands from clients so we can relay FEEDBACK back to them
#define HUB_MAX_PENDING_CMDS 128
typedef struct {
    int cmdid;
    struct sockaddr_in client_addr;
    char module_id[HUB_MODULE_ID_LEN];
    long long issued_ms;
} PendingClientCmd;
static PendingClientCmd g_pending_cmds[HUB_MAX_PENDING_CMDS];

// ---------- time helper ----------

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

// ---------- webhook / Discord helpers ----------

void hub_udp_set_webhook_url(const char *url)
{
    if (!url) return;
    pthread_mutex_lock(&g_mutex);
    size_t len = strlen(url);
    if (len >= sizeof(g_webhook_url)) len = sizeof(g_webhook_url) - 1;
    memmove(g_webhook_url, url, len);
    g_webhook_url[len] = '\0';
    pthread_mutex_unlock(&g_mutex);
}

static void trigger_discord_alert(const char* module_id, const char* event_type, 
                                  const char* door, const char* state)
{
    if (g_webhook_url[0] == '\0') {
        return; // No webhook URL set
    }
    char alert_msg[256];
    snprintf(alert_msg, sizeof(alert_msg), 
             "[%s] %s %s is now %s", module_id, door, event_type, state);
    sendDiscordAlert(g_webhook_url, alert_msg);
}

// ---------- door status helpers ----------

static HubDoorStatus *find_or_create_door(const char *module_id)
{
    for (int i = 0; i < HUB_MAX_DOORS; i++) {
        if (g_doors[i].known &&
            strncmp(g_doors[i].module_id, module_id, HUB_MODULE_ID_LEN) == 0) {
            return &g_doors[i];
        }
    }
    for (int i = 0; i < HUB_MAX_DOORS; i++) {
        if (!g_doors[i].known) {
            memset(&g_doors[i], 0, sizeof(g_doors[i]));
            snprintf(g_doors[i].module_id, sizeof(g_doors[i].module_id),
                     "%s", module_id);
            g_doors[i].known = true;
            return &g_doors[i];
        }
    }
    return NULL;
}

// ---------- pending client-command map ----------

static void register_client_command(int cmdid, const char *module_id,
                                    struct sockaddr_in *client_addr)
{
    int slot = 0;
    long long oldest_ms = g_pending_cmds[0].issued_ms;

    for (int i = 0; i < HUB_MAX_PENDING_CMDS; i++) {
        if (g_pending_cmds[i].cmdid == 0) {
            slot = i;
            break;
        }
        if (g_pending_cmds[i].issued_ms < oldest_ms) {
            oldest_ms = g_pending_cmds[i].issued_ms;
            slot = i;
        }
    }

    g_pending_cmds[slot].cmdid = cmdid;
    g_pending_cmds[slot].client_addr = *client_addr;
    snprintf(g_pending_cmds[slot].module_id,
             sizeof(g_pending_cmds[slot].module_id), "%s", module_id);
    g_pending_cmds[slot].issued_ms = now_ms();
}

// Lookup and remove a client command by module_id and cmdid
static struct sockaddr_in *get_and_clear_client_cmd(const char *module_id,
                                                    int cmdid)
{
    for (int i = 0; i < HUB_MAX_PENDING_CMDS; i++) {
        if (g_pending_cmds[i].cmdid == cmdid &&
            strncmp(g_pending_cmds[i].module_id, module_id,
                    HUB_MODULE_ID_LEN) == 0) {
            static struct sockaddr_in result;
            result = g_pending_cmds[i].client_addr;
            g_pending_cmds[i].cmdid = 0; // free
            return &result;
        }
    }
    return NULL;
}

// ---------- history ----------

static void add_history(const char *module_id, const char *line, long long t)
{
    HubEvent *e = &g_history[g_hist_head];
    e->timestamp_ms = t;
    snprintf(e->module_id, sizeof(e->module_id), "%s", module_id);
    snprintf(e->line, sizeof(e->line), "%s", line);

    g_hist_head = (g_hist_head + 1) % HUB_MAX_HISTORY;
    if (g_hist_count < HUB_MAX_HISTORY) {
        g_hist_count++;
    }
}

// ---------- parse helpers ----------

static void parse_d_state(const char *token,
                          bool *p_open, bool *p_locked)
{
    // token format: "D0=OPEN,LOCKED" or "D1=CLOSED,UNLOCKED"
    const char *eq = strchr(token, '=');
    if (!eq) return;
    const char *states = eq + 1;
    char first[32] = {0};
    char second[32] = {0};

    const char *comma = strchr(states, ',');
    if (comma) {
        size_t len1 = (size_t)(comma - states);
        size_t len2 = strlen(comma + 1);
        if (len1 >= sizeof(first)) len1 = sizeof(first) - 1;
        if (len2 >= sizeof(second)) len2 = sizeof(second) - 1;
        memcpy(first, states, len1);
        first[len1] = '\0';
        memcpy(second, comma + 1, len2);
        second[len2] = '\0';
    } else {
        strncpy(first, states, sizeof(first)-1);
        first[sizeof(first)-1] = '\0';
        second[0] = '\0';
    }

    if (strcmp(first, "OPEN") == 0) {
        *p_open = true;
    } else if (strcmp(first, "CLOSED") == 0) {
        *p_open = false;
    }
    if (strcmp(second, "LOCKED") == 0) {
        *p_locked = true;
    } else if (strcmp(second, "UNLOCKED") == 0) {
        *p_locked = false;
    }

    if (second[0] == '\0') {
        if (strcmp(first, "LOCKED") == 0) {
            *p_locked = true;
        } else if (strcmp(first, "UNLOCKED") == 0) {
            *p_locked = false;
        }
    }
}

// ---------- endpoint helpers (door module -> IP:port) ----------

static HubEndpoint *hub_find_endpoint(const char *module_id)
{
    for (int i = 0; i < g_num_endpoints; i++) {
        if (strcmp(g_endpoints[i].module_id, module_id) == 0) {
            return &g_endpoints[i];
        }
    }
    return NULL;
}

static void hub_update_endpoint(const char *module_id,
                                const struct sockaddr_in *src)
{
    if (!module_id || !src) return;

    HubEndpoint *ep = hub_find_endpoint(module_id);
    if (!ep) {
        if (g_num_endpoints >= HUB_MAX_MODULES) {
            fprintf(stderr, "[hub_udp] Endpoint table full; cannot track %s\n",
                    module_id);
            return;
        }
        ep = &g_endpoints[g_num_endpoints++];
        memset(ep, 0, sizeof(*ep));
        strncpy(ep->module_id, module_id, sizeof(ep->module_id) - 1);
    }

    ep->addr = *src;
    ep->has_addr = true;

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src->sin_addr, ip, sizeof(ip));
    fprintf(stderr, "[hub_udp] Endpoint for %s is %s:%u\n",
            module_id, ip, ntohs(src->sin_port));
}

// Forward the COMMAND line to the door module's last-known endpoint
static bool hub_forward_command_to_module(const char *module_id,
                                          const char *line)
{
    HubEndpoint *ep = hub_find_endpoint(module_id);
    if (!ep || !ep->has_addr) {
        fprintf(stderr,
                "[hub_udp] No endpoint known for module %s; cannot forward COMMAND\n",
                module_id ? module_id : "(null)");
        return false;
    }

    if (g_sock < 0) {
        fprintf(stderr,
                "[hub_udp] Hub main socket not valid; cannot send COMMAND\n");
        return false;
    }

    ssize_t sent = sendto(g_sock,
                          line, strlen(line), 0,
                          (struct sockaddr *)&ep->addr,
                          sizeof(ep->addr));
    if (sent < 0) {
        perror("[hub_udp] sendto (forward COMMAND)");
        return false;
    }

    fprintf(stderr, "[hub_udp] Forwarded COMMAND to %s at %s:%u: '%s'\n",
            module_id,
            inet_ntoa(ep->addr.sin_addr),
            ntohs(ep->addr.sin_port),
            line);
    return true;
}

// ---------- offline detection ----------

static void check_offline_modules(void)
{
    long long now = now_ms();

    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < HUB_MAX_DOORS; i++) {
        if (!g_doors[i].known) continue;

        bool should_be_offline =
            (now - g_doors[i].last_heartbeat_ms) > HUB_OFFLINE_TIMEOUT_MS;

        if (should_be_offline && !g_doors[i].offline) {
            fprintf(stderr,
                    "[hub_offline_check] Module %s went OFFLINE (no heartbeat for %lld ms)\n",
                    g_doors[i].module_id,
                    now - g_doors[i].last_heartbeat_ms);
            g_doors[i].offline = true;
            g_doors[i].last_online_ms = now;

            char event[256];
            snprintf(event, sizeof(event),
                     "%s EVENT SYSTEM OFFLINE\n", g_doors[i].module_id);
            add_history(g_doors[i].module_id, event, now);
            trigger_discord_alert(g_doors[i].module_id,
                                  "SYSTEM", "MODULE", "OFFLINE");
        } else if (!should_be_offline && g_doors[i].offline) {
            fprintf(stderr,
                    "[hub_offline_check] Module %s came back ONLINE\n",
                    g_doors[i].module_id);
            g_doors[i].offline = false;

            char event[256];
            snprintf(event, sizeof(event),
                     "%s EVENT SYSTEM ONLINE\n", g_doors[i].module_id);
            add_history(g_doors[i].module_id, event, now);
            trigger_discord_alert(g_doors[i].module_id,
                                  "SYSTEM", "MODULE", "ONLINE");
        }
    }
    pthread_mutex_unlock(&g_mutex);
}

// ---------- line handler ----------

static void handle_line(char *line, const char *raw,
                        struct sockaddr_in *src, int fd)
{
    (void)fd;
    long long t = now_ms();

    char *save = NULL;
    char *mod  = strtok_r(line, " \t\r\n", &save);
    if (!mod) return;
    char *type = strtok_r(NULL, " \t\r\n", &save);
    if (!type) return;

    pthread_mutex_lock(&g_mutex);

    // Any non-COMMAND from a module (HELLO/EVENT/HEARTBEAT/FEEDBACK)
    // updates our endpoint table with that module's IP:port.
    if (src && strcmp(type, "COMMAND") != 0) {
        hub_update_endpoint(mod, src);
    }

    HubDoorStatus *door = find_or_create_door(mod);
    if (!door) {
        add_history(mod, "<NO-STATE> (untracked)", t);
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    // Only treat non-COMMAND packets as coming from the door module
    // when updating door->last_addr; COMMAND packets are typically
    // from the Node server, and would otherwise clobber this.
    if (src && strcmp(type, "COMMAND") != 0) {
        door->last_addr = *src;
        door->has_last_addr = 1;
    }

    char hist_line[HUB_LINE_LEN];
    snprintf(hist_line, sizeof(hist_line), "%s %s", mod, type);
    add_history(mod, hist_line, t);

    if (strcmp(type, "HEARTBEAT") == 0) {
        char *tok = NULL;
        char hb_buf[HUB_LINE_LEN] = {0};
        while ((tok = strtok_r(NULL, " \t\r\n", &save)) != NULL) {
            if (strncmp(tok, "D0=", 3) == 0) {
                parse_d_state(tok, &door->d0_open, &door->d0_locked);
            } else if (strncmp(tok, "D1=", 3) == 0) {
                parse_d_state(tok, &door->d1_open, &door->d1_locked);
            }
            if (hb_buf[0] != '\0')
                strncat(hb_buf, " ",
                        sizeof(hb_buf)-strlen(hb_buf)-1);
            strncat(hb_buf, tok,
                    sizeof(hb_buf)-strlen(hb_buf)-1);
        }
        door->last_heartbeat_ms = t;
        if (hb_buf[0] != '\0') {
            snprintf(door->last_heartbeat_line,
                     sizeof(door->last_heartbeat_line), "%s", hb_buf);
        } else {
            snprintf(door->last_heartbeat_line,
                     sizeof(door->last_heartbeat_line), "%s", hist_line);
        }
    } else if (strcmp(type, "EVENT") == 0) {
        char *which = strtok_r(NULL, " \t\r\n", &save);
        char *what  = strtok_r(NULL, " \t\r\n", &save);
        char *state = strtok_r(NULL, " \t\r\n", &save);
        if (which && what && state) {
            bool *p_open  = NULL;
            bool *p_locked= NULL;

            if (strcmp(which, "D0") == 0) {
                p_open   = &door->d0_open;
                p_locked = &door->d0_locked;
            } else if (strcmp(which, "D1") == 0) {
                p_open   = &door->d1_open;
                p_locked = &door->d1_locked;
            }

            if (p_open && p_locked) {
                if (strcmp(what, "DOOR") == 0) {
                    if (strcmp(state, "OPEN") == 0) {
                        *p_open = true;
                        trigger_discord_alert(mod, what, which, state);
                    } else if (strcmp(state, "CLOSED") == 0) {
                        *p_open = false;
                        trigger_discord_alert(mod, what, which, state);
                    }
                } else if (strcmp(what, "LOCK") == 0) {
                    if (strcmp(state, "LOCKED") == 0) {
                        *p_locked = true;
                        trigger_discord_alert(mod, what, which, state);
                    } else if (strcmp(state, "UNLOCKED") == 0) {
                        *p_locked = false;
                        trigger_discord_alert(mod, what, which, state);
                    }
                }
            }
        }
        door->last_event_ms = t;
    } else if (strcmp(type, "FEEDBACK") == 0) {
        char *cmdid_s = strtok_r(NULL, " \t\r\n", &save);
        char *target  = strtok_r(NULL, " \t\r\n", &save);
        char *action  = strtok_r(NULL, " \t\r\n", &save);
        int cmdid = 0;
        if (cmdid_s) cmdid = atoi(cmdid_s);
        if (target && action) {
            snprintf(door->last_feedback_target,
                     sizeof(door->last_feedback_target), "%s", target);
            snprintf(door->last_feedback_action,
                     sizeof(door->last_feedback_action), "%s", action);
            door->last_feedback_ms    = t;
            door->last_feedback_cmdid = cmdid;

            char fbline[HUB_LINE_LEN];
            snprintf(fbline, sizeof(fbline),
                     "FEEDBACK %d %s %s", cmdid, target, action);
            add_history(mod, fbline, t);

            struct sockaddr_in *client_addr =
                get_and_clear_client_cmd(mod, cmdid);
            if (client_addr) {
                char relay_msg[256];
                snprintf(relay_msg, sizeof(relay_msg),
                         "%s FEEDBACK %d %s %s\n",
                         mod, cmdid, target, action);

                pthread_mutex_unlock(&g_mutex);
                int relay_sock = socket(AF_INET, SOCK_DGRAM, 0);
                if (relay_sock >= 0) {
                    sendto(relay_sock, relay_msg, strlen(relay_msg), 0,
                           (struct sockaddr *)client_addr,
                           sizeof(*client_addr));
                    close(relay_sock);
                }
                pthread_mutex_lock(&g_mutex);
            }

            pthread_cond_broadcast(&g_feedback_cond);
        }
    } else if (strcmp(type, "COMMAND") == 0) {
        // COMMAND <CMDID> <TARGET> <ACTION> from Node → forward to door
        char *cmdid_s = strtok_r(NULL, " \t\r\n", &save);
        char *target  = strtok_r(NULL, " \t\r\n", &save);
        char *action  = strtok_r(NULL, " \t\r\n", &save);

        if (cmdid_s && src && target && action) {
            int client_cmdid = atoi(cmdid_s);
            register_client_command(client_cmdid, mod, src);

            // Forward the ORIGINAL line (raw) to the module, so the
            // cmdid stays the same from Node → door → FEEDBACK
            pthread_mutex_unlock(&g_mutex);
            hub_forward_command_to_module(mod, raw);
            pthread_mutex_lock(&g_mutex);
        }
    } else {
        // HELLO or unknown, just history+timestamp
        door->last_event_ms = t;
    }

    pthread_mutex_unlock(&g_mutex);
}

// ---------- receiver thread ----------

static void *udp_thread(void *arg)
{
    (void)arg;
    fprintf(stderr,
            "[hub_udp_thread] Listener thread started, waiting for incoming datagrams...\n");

    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);
    char buf[HUB_LINE_LEN];
    char raw[HUB_LINE_LEN];

    while (!g_stopping) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        if (g_sock >= 0) { FD_SET(g_sock, &rfds); maxfd = g_sock; }
        if (g_sock2 >= 0) {
            FD_SET(g_sock2, &rfds);
            if (g_sock2 > maxfd) maxfd = g_sock2;
        }

        struct timeval tv;
        tv.tv_sec = 1; tv.tv_usec = 0;
        int r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("hub_udp: select");
            sleepForMs(1);
            continue;
        } else if (r == 0) {
            check_offline_modules();
            continue;
        }

        int fd = -1;
        if (g_sock >= 0 && FD_ISSET(g_sock, &rfds)) fd = g_sock;
        else if (g_sock2 >= 0 && FD_ISSET(g_sock2, &rfds)) fd = g_sock2;
        if (fd < 0) continue;

        while (1) {
            ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT,
                                 (struct sockaddr *)&src, &src_len);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (errno == EINTR) continue;
                perror("hub_udp: recvfrom");
                break;
            }
            if (n == 0) break;
            buf[n] = '\0';
            memcpy(raw, buf, n + 1);

            char src_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &src.sin_addr, src_ip, INET_ADDRSTRLEN);
            fprintf(stderr,
                    "[hub_udp_thread] RECEIVED: %zd bytes from %s:%u on fd=%d: '%s'\n",
                    n, src_ip, ntohs(src.sin_port), fd, buf);

            handle_line(buf, raw, &src, fd);
        }

        check_offline_modules();
    }

    return NULL;
}

// ---------- public API ----------

bool hub_udp_init(uint16_t listen_port1, uint16_t listen_port2)
{
    fprintf(stderr,
            "[hub_udp_init] START: listen_port1=%u, listen_port2=%u\n",
            listen_port1, listen_port2);
    if (g_sock >= 0 || g_sock2 >= 0) {
        fprintf(stderr, "hub_udp_init: already initialized\n");
        return false;
    }
    if (!discordStart()) {
        fprintf(stderr, "discordStart() failed\n");
    }
    g_listen_port = listen_port1;

    int s1 = socket(AF_INET, SOCK_DGRAM, 0);
    if (s1 < 0) {
        perror("[hub_udp_init] socket");
        fprintf(stderr,
                "[hub_udp_init] ERROR: Cannot create socket for port1\n");
        return false;
    }
    fprintf(stderr, "[hub_udp_init] Socket 1 created: fd=%d\n", s1);

    struct sockaddr_in addr1;
    memset(&addr1, 0, sizeof(addr1));
    addr1.sin_family      = AF_INET;
    addr1.sin_port        = htons(listen_port1);
    addr1.sin_addr.s_addr = htonl(INADDR_ANY);

    fprintf(stderr, "[hub_udp_init] Binding port1 (%u)...\n", listen_port1);
    if (bind(s1, (struct sockaddr *)&addr1, sizeof(addr1)) < 0) {
        perror("[hub_udp_init] bind port1");
        fprintf(stderr,
                "[hub_udp_init] ERROR: Cannot bind port1 %u (already in use?)\n",
                listen_port1);
        close(s1);
        return false;
    }
    fprintf(stderr, "[hub_udp_init] Port1 (%u) bound successfully\n",
            listen_port1);

    g_sock = s1;

    if (listen_port2 != 0) {
        int s2 = socket(AF_INET, SOCK_DGRAM, 0);
        if (s2 < 0) {
            perror("[hub_udp_init] socket2");
            fprintf(stderr,
                    "[hub_udp_init] ERROR: Cannot create socket for port2\n");
            close(g_sock);
            g_sock = -1;
            return false;
        }
        fprintf(stderr, "[hub_udp_init] Socket 2 created: fd=%d\n", s2);
        struct sockaddr_in addr2;
        memset(&addr2, 0, sizeof(addr2));
        addr2.sin_family      = AF_INET;
        addr2.sin_port        = htons(listen_port2);
        addr2.sin_addr.s_addr = htonl(INADDR_ANY);
        fprintf(stderr, "[hub_udp_init] Binding port2 (%u)...\n", listen_port2);
        if (bind(s2, (struct sockaddr *)&addr2, sizeof(addr2)) < 0) {
            perror("[hub_udp_init] bind port2");
            fprintf(stderr,
                    "[hub_udp_init] ERROR: Cannot bind port2 %u (already in use?)\n",
                    listen_port2);
            close(s2);
            close(g_sock);
            g_sock = -1;
            return false;
        }
        fprintf(stderr, "[hub_udp_init] Port2 (%u) bound successfully\n",
                listen_port2);
        g_sock2 = s2;
    }

    g_stopping = 0;

    pthread_mutex_lock(&g_mutex);
    memset(g_doors, 0, sizeof(g_doors));
    memset(g_history, 0, sizeof(g_history));
    g_hist_head  = 0;
    g_hist_count = 0;
    memset(g_endpoints, 0, sizeof(g_endpoints));
    g_num_endpoints = 0;
    pthread_mutex_unlock(&g_mutex);

    fprintf(stderr, "[hub_udp_init] Creating listener thread...\n");
    if (pthread_create(&g_thread_id, NULL, udp_thread, NULL) != 0) {
        perror("[hub_udp_init] pthread_create");
        fprintf(stderr,
                "[hub_udp_init] ERROR: Failed to create listener thread\n");
        if (g_sock2 >= 0) close(g_sock2);
        if (g_sock  >= 0) close(g_sock);
        g_sock = -1; g_sock2 = -1;
        return false;
    }
    fprintf(stderr,
            "[hub_udp_init] Listener thread created successfully\n");
    fprintf(stderr,
            "[hub_udp_init] HUB INIT COMPLETE: listening on ports %u and %u\n",
            listen_port1, listen_port2);

    return true;
}

void hub_udp_shutdown(void)
{
    if (g_sock < 0 && g_sock2 < 0) return;

    g_stopping = 1;
    pthread_join(g_thread_id, NULL);
    if (g_sock  >= 0) { close(g_sock);  g_sock  = -1; }
    if (g_sock2 >= 0) { close(g_sock2); g_sock2 = -1; }
    discordCleanup();
}

bool hub_udp_get_status(const char *module_id, HubDoorStatus *out)
{
    if (!module_id || !out) return false;

    bool found = false;
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < HUB_MAX_DOORS; i++) {
        if (g_doors[i].known &&
            strncmp(g_doors[i].module_id, module_id,
                    HUB_MODULE_ID_LEN) == 0) {
            *out = g_doors[i];
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return found;
}

int hub_udp_get_history(HubEvent *out, int max_events)
{
    if (!out || max_events <= 0) return 0;

    pthread_mutex_lock(&g_mutex);
    int count = (g_hist_count < max_events) ? g_hist_count : max_events;

    int start = (g_hist_head - g_hist_count + HUB_MAX_HISTORY)
                % HUB_MAX_HISTORY;

    for (int i = 0; i < count; i++) {
        int idx = (start + i) % HUB_MAX_HISTORY;
        out[i] = g_history[idx];
    }
    pthread_mutex_unlock(&g_mutex);

    return count;
}

// Existing hub_udp_send_command (used by hub CLI / internal code).
// This now co-exists with the forwarding path used by Node commands.
bool hub_udp_send_command(const char *module_id,
                          const char *target, const char *action)
{
    if (!module_id || !target || !action) return false;

    const int ACK_TIMEOUT_MS = 500;
    const int ACK_RETRIES    = 2;

    pthread_mutex_lock(&g_mutex);
    HubDoorStatus *door = NULL;
    for (int i = 0; i < HUB_MAX_DOORS; i++) {
        if (g_doors[i].known &&
            strncmp(g_doors[i].module_id, module_id,
                    HUB_MODULE_ID_LEN) == 0) {
            door = &g_doors[i];
            break;
        }
    }
    if (!door || !door->has_last_addr) {
        pthread_mutex_unlock(&g_mutex);
        return false;
    }

    struct sockaddr_in dest = door->last_addr;
    int cmdid = g_next_cmdid++;
    pthread_mutex_unlock(&g_mutex);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "%s COMMAND %d %s %s\n", module_id, cmdid, target, action);

    int attempt = 0;
    while (attempt <= ACK_RETRIES) {
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0) {
            attempt++;
            sleepForMs(1);
            continue;
        }
        ssize_t n = sendto(s, buf, strlen(buf), 0,
                           (struct sockaddr *)&dest, sizeof(dest));
        close(s);
        if (n != (ssize_t)strlen(buf)) {
            attempt++;
            sleepForMs(20 * (1 << attempt));
            continue;
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += (ACK_TIMEOUT_MS / 1000);
        ts.tv_nsec += (ACK_TIMEOUT_MS % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }

        pthread_mutex_lock(&g_mutex);
        int rc = 0;
        while (door->last_feedback_cmdid < cmdid) {
            rc = pthread_cond_timedwait(&g_feedback_cond, &g_mutex, &ts);
            if (rc == ETIMEDOUT) break;
        }

        bool got = false;
        if (door->last_feedback_cmdid >= cmdid) {
            if (strncmp(door->last_feedback_target, target,
                        sizeof(door->last_feedback_target)) == 0 &&
                strncmp(door->last_feedback_action, action,
                        sizeof(door->last_feedback_action)) == 0) {
                got = true;
            }
        }
        pthread_mutex_unlock(&g_mutex);

        if (got) {
            LED_enqueue_hub_command_success();
            return true;
        }

        attempt++;
        sleepForMs(50 * attempt);
    }

    LED_enqueue_blink_red_n(5, 2, 50);
    LED_enqueue_status_network_error();
    return false;
}