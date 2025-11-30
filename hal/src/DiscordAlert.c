#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdatomic.h>

#include "hal/DiscordAlert.h"
#include "hal/timing.h"

// Discord Alert sending handling using libcurl
static const char* webhook_URL = "https://discord.com/api/webhooks/1444219627461673080/rrr5SoaN1RpNC_PGoIH_mFWFV8fB4PosUd6qGC24M3zfg6nsDnvXAhyTxtr5qDiZOJy2";
bool discordStart(void){
    const char* webhook = webhook_URL;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (!webhook) {
        fprintf(stderr, "discordStart: webhook_url is NULL\n");
        return false;
    }
    return true;
}

void discordCleanup(void){
    curl_global_cleanup();
}

void sendDiscordAlert(const char *webhook_url, const char *msg)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init() failed\n");
        return;
    }

    // JSON payload: { "content": "your message" }
    char json[512];
    snprintf(json, sizeof(json), "{\"content\":\"%s\"}", msg);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, webhook_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Discord webhook failed: %s\n", curl_easy_strerror(res));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}
// Door alert thread function. We intentionally do NOT include or reference
// Door_t here. Instead, callers provide an `AlertMsgProvider` callback that
// returns a freshly-allocated string describing the door state; this keeps
// DiscordAlert decoupled from application internals.
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

    // Optional: send initial status once at startup
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