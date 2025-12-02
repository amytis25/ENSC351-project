#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "discord_alert.h"

/* Simple provider that returns changing messages on consecutive calls.
 * The provider must return a freshly allocated string (or NULL).
 */
static int call_count = 0;
static char *test_provider(void *ctx) {
    (void)ctx;
    call_count++;
    if (call_count == 1) return strdup("Test alert: initial state");
    if (call_count == 2) return strdup("Test alert: second state");
    if (call_count == 3) return strdup("Test alert: second state"); // same as previous -> should not resend
    if (call_count == 4) return strdup("Test alert: final state");
    /* after that, return NULL to avoid new alerts */
    return NULL;
}

int main(int argc, char **argv) {
    const char *webhook = (argc > 1) ? argv[1] : "https://discord.com/api/webhooks/1445277245743697940/-DWPsZbIoDTyo1iaXRW3Vo4URqJ1RpkjGQ4ijXENNeYcM9bNHUj90aunxeSU5GsnoZ_M";

    printf("Using webhook URL: %s\n", webhook);

    if (!discordStart()) {
        fprintf(stderr, "discordStart() failed\n");
        return 1;
    }

    if (!startDoorAlertMonitor(test_provider, NULL, webhook)) {
        fprintf(stderr, "startDoorAlertMonitor() failed\n");
        discordCleanup();
        return 1;
    }

    /* Let monitor run for a few seconds so we can observe several checks */
    printf("Monitor started, sleeping 6s to observe alerts...\n");
    sleep(6);

    stopDoorAlertMonitor();
    discordCleanup();
    printf("Monitor stopped and cleaned up.\n");
    return 0;
}