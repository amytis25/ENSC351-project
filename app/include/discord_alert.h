#ifndef APP_DISCORD_ALERT_H
#define APP_DISCORD_ALERT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Application-level Discord alert API (moved from HAL into app).
// This API provides the same functions previously exposed by the HAL
// but lives in the application so that webhook credentials and
// monitor lifecycle are owned by `door_system`.

typedef char *(*AlertMsgProvider)(void *ctx);

bool discordStart(void);
void discordCleanup(void);
void sendDiscordAlert(const char *webhookURL, const char *msg);

bool startDoorAlertMonitor(AlertMsgProvider provider, void *ctx, const char *webhook_url);
void stopDoorAlertMonitor(void);

#endif // APP_DISCORD_ALERT_H
