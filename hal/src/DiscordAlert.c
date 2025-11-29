#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdatomic.h>

#include "hal/DiscordAlert.h"
#include "hal/timing.h"
#include "doorMod.h"

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
// Door alert thread function
typedef struct {
    Door_t door;
    const char *webhook_url;
} DoorMonitorCtx;

static pthread_t        doorThreadId;
static atomic_bool      doorThreadRunning = false;
static DoorMonitorCtx  *g_ctx = NULL;

static const char *door_state_to_str(DoorState_t s)
{
    switch (s) {
    case OPEN:     return "OPEN";
    case LOCKED:   return "LOCKED";
    case UNLOCKED: return "UNLOCKED";
    default:       return "UNKNOWN";
    }
}
void* doorAlertThread(void* arg) {
    DoorMonitorCtx *ctx = (DoorMonitorCtx *)arg;
    Door_t *door = &ctx->door;
    const char *webhook_url = ctx->webhook_url;

    DoorState_t last_state = UNKNOWN;

    // Optional: send initial status once at startup
    *door = get_door_status(door);
    last_state = door->state;
    {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Door %d initial state: %s",
                 door->id, door_state_to_str(door->state));
        sendDiscordAlert(webhook_url, msg);
    }

    while (atomic_load(&doorThreadRunning)) {
        *door = get_door_status(door);

        if (door->state != last_state) {
            last_state = door->state;

            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Door %d changed state: %s",
                     door->id, door_state_to_str(door->state));
            sendDiscordAlert(webhook_url, msg);
        }

        // Polling period; adjust if needed
        sleepForMs(500);
    }

    return NULL;
}
bool startDoorAlertMonitor(Door_t door, const char *webhook_url) {
    if (atomic_load(&doorThreadRunning)) {
        // Already running
        return false;
    }

    g_ctx = malloc(sizeof(DoorMonitorCtx));
    if (!g_ctx) {
        return false;
    }
    g_ctx->door = door;
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
    if (!atomic_load(&doorThreadRunning)) {
        return;
    }

    atomic_store(&doorThreadRunning, false);
    pthread_join(doorThreadId, NULL);

    if (g_ctx) {
        free(g_ctx);
        g_ctx = NULL;
    }
}