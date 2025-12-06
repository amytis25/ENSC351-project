#ifndef APP_DISCORD_ALERT_H
#define APP_DISCORD_ALERT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Application-level Discord alert API
typedef char *(*AlertMsgProvider)(void *ctx);

bool discordStart(void);
void discordCleanup(void);
void sendDiscordAlert(const char *webhookURL, const char *msg);

/**
 * Bind Discord webhook traffic to a specific network device.
 * Pass NULL or empty string to use any available interface (default).
 * Requires CAP_NET_RAW capability.
 * 
 * @param device  Device name (e.g., "wlan0", "eth0"), or NULL
 */
void discord_set_device(const char *device);

bool startDoorAlertMonitor(AlertMsgProvider provider, void *ctx, const char *webhook_url);
void stopDoorAlertMonitor(void);

#endif // APP_DISCORD_ALERT_H
