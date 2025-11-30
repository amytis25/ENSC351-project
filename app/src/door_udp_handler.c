/* app/src/door_udp_handler.c
 * Application-level UDP command handler that uses the HAL transport to
 * receive COMMAND messages and send FEEDBACK. Keeps application logic
 * (lock/unlock/status) in the app layer.
 */
#include <stdio.h>
#include <string.h>
#include "hal/door_udp.h"
#include "include/doorMod.h"

static void app_command_handler(const char *module, int cmdid, const char *target, const char *action, void *ctx)
{
    (void)ctx;
    Door_t d = { .state = UNKNOWN };
    if (action && strcmp(action, "LOCK") == 0) {
        d = lockDoor(&d);
    } else if (action && strcmp(action, "UNLOCK") == 0) {
        d = unlockDoor(&d);
    } else if (action && strcmp(action, "STATUS") == 0) {
        d = get_door_status(&d);
    }
    // Send FEEDBACK via HAL transport
    door_udp_send_feedback(module, cmdid, target ? target : "", action ? action : "");
}

bool app_udp_handler_init(void)
{
    return door_udp_register_command_handler(app_command_handler, NULL);
}
