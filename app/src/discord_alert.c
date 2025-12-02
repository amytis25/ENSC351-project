#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <net/if.h>

#include "discord_alert.h"
#include "hal/timing.h"

/* Device binding for Discord webhook traffic (optional) */
static char g_discord_device[IFNAMSIZ] = {0};
static pthread_mutex_t g_discord_device_lock = PTHREAD_MUTEX_INITIALIZER;

/* Socket creation callback for curl to bind to a specific interface */
static curl_socket_t socket_callback_bind_device(void *clientp, curlsocktype purpose,
                                          struct curl_sockaddr *address)
{
    (void)purpose;
    (void)clientp;

    /* Null check - address should always be valid, but safer to check */
    if (!address) {
        fprintf(stderr, "Discord: socket_callback_bind_device called with NULL address\n");
        return CURL_SOCKET_BAD;
    }

    /* Create the socket normally */
    curl_socket_t sock = socket(address->family, address->socktype, address->protocol);
    if (sock == CURL_SOCKET_BAD) {
        return CURL_SOCKET_BAD;
    }

    /* Apply device binding if configured */
    pthread_mutex_lock(&g_discord_device_lock);
    if (g_discord_device[0] != '\0') {
        if (setsockopt((int)sock, SOL_SOCKET, SO_BINDTODEVICE, g_discord_device, 
                       strlen(g_discord_device) + 1) != 0) {
            fprintf(stderr, "Discord: Failed to bind socket to device '%s'\n", g_discord_device);
            close((int)sock);
            pthread_mutex_unlock(&g_discord_device_lock);
            return CURL_SOCKET_BAD;
        } else {
            fprintf(stderr, "Discord: Socket bound to device '%s'\n", g_discord_device);
        }
    }
    pthread_mutex_unlock(&g_discord_device_lock);

    return sock;
}

/**
 * Set the network device to bind Discord webhook traffic to.
 * Pass NULL or empty string to use any interface (default).
 * Requires CAP_NET_RAW capability.
 */
void discord_set_device(const char *device)
{
    pthread_mutex_lock(&g_discord_device_lock);
    if (!device || device[0] == '\0') {
        g_discord_device[0] = '\0';
    } else {
        size_t len = strlen(device);
        if (len >= IFNAMSIZ) {
            fprintf(stderr, "Discord: device name '%s' too long\n", device);
        } else {
            strncpy(g_discord_device, device, IFNAMSIZ - 1);
            g_discord_device[IFNAMSIZ - 1] = '\0';
        }
    }
    pthread_mutex_unlock(&g_discord_device_lock);
}

// Discord Alert sending handling using libcurl
bool discordStart(void){
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
}

void discordCleanup(void){
    curl_global_cleanup();
}

void sendDiscordAlert(const char *webhook_url, const char *msg)
{
    if (!webhook_url || !msg) return;
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init() failed\n");
        return;
    }

    char json[512];
    snprintf(json, sizeof(json), "{\"content\":\"%s\"}", msg);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, webhook_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    
    /* Set socket creation callback to bind to wlan0 if configured */
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, socket_callback_bind_device);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Discord webhook failed: %s\n", curl_easy_strerror(res));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

// Door alert thread function. The provider callback returns a freshly
// allocated string (or NULL). The app owns the provider and the
// webhook URL.
typedef struct {
    AlertMsgProvider provider;
    void *provider_ctx;
    const char *webhook_url;
} DoorMonitorCtx;

static pthread_t        doorThreadId;
static atomic_bool      doorThreadRunning = false;
static DoorMonitorCtx  *g_ctx = NULL;

void* doorAlertThread(void* arg) {
    DoorMonitorCtx *ctx = (DoorMonitorCtx *)arg;
    const char *webhook_url = ctx->webhook_url;
    char *last_msg = NULL;

    if (ctx->provider) {
        char *m = ctx->provider(ctx->provider_ctx);
        if (m) {
            sendDiscordAlert(webhook_url, m);
            last_msg = strdup(m);
            free(m);
        }
    }

    while (atomic_load(&doorThreadRunning)) {
        if (ctx->provider) {
            char *m = ctx->provider(ctx->provider_ctx);
            if (m) {
                if (!last_msg || strcmp(m, last_msg) != 0) {
                    sendDiscordAlert(webhook_url, m);
                    free(last_msg);
                    last_msg = strdup(m);
                }
                free(m);
            }
        }
        sleepForMs(500);
    }

    free(last_msg);
    return NULL;
}

bool startDoorAlertMonitor(AlertMsgProvider provider, void *provider_ctx, const char *webhook_url) {
    if (atomic_load(&doorThreadRunning)) {
        return false;
    }

    g_ctx = malloc(sizeof(DoorMonitorCtx));
    if (!g_ctx) return false;

    g_ctx->provider = provider;
    g_ctx->provider_ctx = provider_ctx;
    g_ctx->webhook_url = webhook_url;

    atomic_store(&doorThreadRunning, true);
    if (pthread_create(&doorThreadId, NULL, doorAlertThread, g_ctx) != 0) {
        atomic_store(&doorThreadRunning, false);
        free(g_ctx);
        g_ctx = NULL;
        return false;
    }
    return true;
}

void stopDoorAlertMonitor() {
    if (!atomic_load(&doorThreadRunning)) return;

    atomic_store(&doorThreadRunning, false);
    pthread_join(doorThreadId, NULL);

    if (g_ctx) {
        free(g_ctx);
        g_ctx = NULL;
    }
}
