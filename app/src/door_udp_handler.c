// app/src/door_udp_handler.c
#include <stdio.h>
#include <string.h>
#include "hal/door_udp.h"
#include "doorMod.h"

static void app_command_handler(const char *module, int cmdid,
                                const char *target, const char *action,
                                void *ctx)
{
    (void)ctx;
    Door_t d = { .state = UNKNOWN };

    const char *out_action = action;
    char action_buf[32];

    if (action && strcmp(action, "LOCK") == 0) {
        d = lockDoor(&d);
        // leave out_action = "LOCK" (means "lock command completed")
    } else if (action && strcmp(action, "UNLOCK") == 0) {
        d = unlockDoor(&d);
        // leave out_action = "UNLOCK"
    } else if (action && strcmp(action, "STATUS") == 0) {
        d = get_door_status(&d);

        // Map our Door_t.state to text
        const char *state_str = "UNKNOWN";
        switch (d.state) {
            case LOCKED:   state_str = "LOCKED";   break;
            case UNLOCKED: state_str = "UNLOCKED"; break;
            case OPEN:     state_str = "OPEN";     break;
            case UNKNOWN:
            default:       state_str = "UNKNOWN";  break;
        }

        // Encode as STATUS_<STATE> so the Node side can parse it
        snprintf(action_buf, sizeof(action_buf), "STATUS_%s", state_str);
        out_action = action_buf;
    }

    // Send FEEDBACK via HAL transport
    door_udp_send_feedback(module,
                           cmdid,
                           target ? target : "",
                           out_action ? out_action : "");
}

bool app_udp_handler_init(void)
{
    return door_udp_register_command_handler(app_command_handler, NULL);
}