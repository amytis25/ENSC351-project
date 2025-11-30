// hub_udp.c
#define _POSIX_C_SOURCE 200809L
#include "hub_udp.h"
#include <curl/curl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <hal/DiscordAlert.h>

static int          g_sock        = -1;
static pthread_t    g_thread_id;
static int          g_listen_port = 0;
static volatile int g_stopping    = 0;
static char         g_webhook_url[512] = {0};  // Discord webhook URL


static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

// Per-door status
static HubDoorStatus g_doors[HUB_MAX_DOORS];

// History ring buffer
static HubEvent g_history[HUB_MAX_HISTORY];
static int      g_hist_head = 0; // next slot to write
static int      g_hist_count = 0;

// ---------- time helper ----------
static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

// ---------- internal helpers ----------
void hub_udp_set_webhook_url(const char *url)
{
    if (!url) return;
    pthread_mutex_lock(&g_mutex);
    snprintf(g_webhook_url, sizeof(g_webhook_url), "%s", url);
    pthread_mutex_unlock(&g_mutex);
}
static void trigger_discord_alert(const char* module_id, const char* event_type, 
                                 const char* door, const char* state) {
    if (g_webhook_url[0] == '\0') {
        return; // No webhook URL set
    }
    char alert_msg[256];
    snprintf(alert_msg, sizeof(alert_msg), 
             "[%s] %s %s is now %s", module_id, door, event_type, state);
    sendDiscordAlert(g_webhook_url, alert_msg);
}

static HubDoorStatus *find_or_create_door(const char *module_id)
{
    // Find existing
    for (int i = 0; i < HUB_MAX_DOORS; i++) {
        if (g_doors[i].known &&
            strncmp(g_doors[i].module_id, module_id, HUB_MODULE_ID_LEN) == 0) {
            return &g_doors[i];
        }
    }
    // Create new
    for (int i = 0; i < HUB_MAX_DOORS; i++) {
        if (!g_doors[i].known) {
            memset(&g_doors[i], 0, sizeof(g_doors[i]));
            snprintf(g_doors[i].module_id, sizeof(g_doors[i].module_id),
                     "%s", module_id);
            g_doors[i].known = true;
            return &g_doors[i];
        }
    }
    // No space
    return NULL;
}

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

static void parse_d_state(const char *token,
                          bool *p_open, bool *p_locked)
{
    // token format: "D0=OPEN,LOCKED" or "D1=CLOSED,UNLOCKED"
    const char *eq = strchr(token, '=');
    if (!eq) return;
    const char *states = eq + 1;
    char first[32] = {0};
    char second[32] = {0};

    // Split on comma
    const char *comma = strchr(states, ',');
    if (!comma) return;
    size_t len1 = (size_t)(comma - states);
    size_t len2 = strlen(comma + 1);

    if (len1 >= sizeof(first)) len1 = sizeof(first) - 1;
    if (len2 >= sizeof(second)) len2 = sizeof(second) - 1;

    memcpy(first, states, len1);
    first[len1] = '\0';
    memcpy(second, comma + 1, len2);
    second[len2] = '\0';

    // Normalize to uppercase-ish compare
    // We can just strcmp with expected strings.
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
}

static void handle_line(char *line)
{
    long long t = now_ms();

    // Tokenize: <MODULE> <TYPE> ...
    char *save = NULL;
    char *mod  = strtok_r(line, " \t\r\n", &save);
    if (!mod) return;
    char *type = strtok_r(NULL, " \t\r\n", &save);
    if (!type) return;

    HubDoorStatus *door = NULL;

    pthread_mutex_lock(&g_mutex);

    door = find_or_create_door(mod);
    if (!door) {
        // No room to track more doors; still store in history
        add_history(mod, "<NO-STATE> (untracked)", t);
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    // Record in history (using the original text would be nicer, but
    // strtok_r modified it; we can reconstruct a simple line).
    // For history, store just "<TYPE> ..." plus module_id.
    // Here we'll just record type + remaining tokens as line.
    char hist_line[HUB_LINE_LEN];
    snprintf(hist_line, sizeof(hist_line), "%s %s", mod, type);
    add_history(mod, hist_line, t);

    if (strcmp(type, "HEARTBEAT") == 0) {
        // Expect tokens like: D0=OPEN,LOCKED D1=CLOSED,UNLOCKED
        char *tok = NULL;
        while ((tok = strtok_r(NULL, " \t\r\n", &save)) != NULL) {
            if (strncmp(tok, "D0=", 3) == 0) {
                parse_d_state(tok, &door->d0_open, &door->d0_locked);
            } else if (strncmp(tok, "D1=", 3) == 0) {
                parse_d_state(tok, &door->d1_open, &door->d1_locked);
            }
        }
        door->last_heartbeat_ms = t;
        snprintf(door->last_heartbeat_line,
                 sizeof(door->last_heartbeat_line),
                 "%s", hist_line);
    } else if (strcmp(type, "EVENT") == 0) {
        // EVENT <D0|D1> <DOOR|LOCK> <STATE>
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

    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);
    char buf[HUB_LINE_LEN];

    while (!g_stopping) {
        ssize_t n = recvfrom(g_sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&src, &src_len);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("hub_udp: recvfrom");
            usleep(1000);
            continue;
        }
        buf[n] = '\0';

        // We modify buf during parsing, so work on a local copy
        handle_line(buf);
    }

    return NULL;
}

// ---------- public API ----------

bool hub_udp_init(uint16_t listen_port)
{
    if (g_sock >= 0) {
        fprintf(stderr, "hub_udp_init: already initialized\n");
        return false;
    }

    g_listen_port = listen_port;

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("hub_udp: socket");
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(listen_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("hub_udp: bind");
        close(s);
        return false;
    }

    g_sock = s;
    g_stopping = 0;

    // Clear state
    pthread_mutex_lock(&g_mutex);
    memset(g_doors, 0, sizeof(g_doors));
    memset(g_history, 0, sizeof(g_history));
    g_hist_head  = 0;
    g_hist_count = 0;
    pthread_mutex_unlock(&g_mutex);

    if (pthread_create(&g_thread_id, NULL, udp_thread, NULL) != 0) {
        perror("hub_udp: pthread_create");
        close(g_sock);
        g_sock = -1;
        return false;
    }

    return true;
}

void hub_udp_shutdown(void)
{
    if (g_sock < 0) return;

    g_stopping = 1;
    // poke the socket by sending a dummy packet to ourselves (optional)

    pthread_join(g_thread_id, NULL);
    close(g_sock);
    g_sock = -1;
}

bool hub_udp_get_status(const char *module_id, HubDoorStatus *out)
{
    if (!module_id || !out) return false;

    bool found = false;
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < HUB_MAX_DOORS; i++) {
        if (g_doors[i].known &&
            strncmp(g_doors[i].module_id, module_id, HUB_MODULE_ID_LEN) == 0) {
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

    // Oldest event index:
    int start = (g_hist_head - g_hist_count + HUB_MAX_HISTORY) % HUB_MAX_HISTORY;

    for (int i = 0; i < count; i++) {
        int idx = (start + i) % HUB_MAX_HISTORY;
        out[i] = g_history[idx];
    }
    pthread_mutex_unlock(&g_mutex);

    return count;
}