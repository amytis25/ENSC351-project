// Stub HAL-side DiscordAlert implementations.
// The real implementations live in the application (`app/`).
// These weak, no-op functions prevent duplicate-link errors while allowing
// the app to provide the real (strong) symbols.

#include <stdbool.h>
#include <stddef.h>

__attribute__((weak)) bool discordStart(void) { return true; }
__attribute__((weak)) void discordCleanup(void) { }
__attribute__((weak)) void sendDiscordAlert(const char *webhook_url, const char *msg) { (void)webhook_url; (void)msg; }
__attribute__((weak)) bool startDoorAlertMonitor(char *(*provider)(void *), void *ctx, const char *webhook_url) { (void)provider; (void)ctx; (void)webhook_url; return false; }
__attribute__((weak)) void stopDoorAlertMonitor(void) { }