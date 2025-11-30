#ifndef DISCORDALERT_H
#define DISCORDALERT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <curl/curl.h>

// Type for a callback that produces an alert message string.
// The callback should return a freshly allocated string (via `strdup`/`malloc`)
// or NULL if no message is available. The caller (DiscordAlert) will free()
// the returned string after use.
typedef char *(*AlertMsgProvider)(void *ctx);

bool discordStart(void);
void discordCleanup(void);
void sendDiscordAlert(const char *webhookURL, const char *msg);

// Start a background monitor that periodically asks `provider` for a
// textual alert message and posts it to `webhook_url` when it changes.
// This API intentionally accepts a provider callback to avoid importing
// door internal types into the HAL; the provider lives in the caller (app).
bool startDoorAlertMonitor(AlertMsgProvider provider, void *ctx, const char *webhook_url);
void stopDoorAlertMonitor(void);

#endif
