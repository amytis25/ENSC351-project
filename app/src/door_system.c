#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include "hal/doorMod.h"
#include "hal/hub_udp.h"
#include "hal/led.h"
#include "hal/door_udp_client.h"
#include <unistd.h>
#include <errno.h>

typedef struct {
    Door_t* door[4];
} door_system_t;

int main(int argc, char *argv[]){
    const char *module_id = (argc > 1) ? argv[1] : "D1";
    const char *hub_ip    = (argc > 2) ? argv[2] : "192.168.7.10";
    bool door_udp_running = false;

    if (!initializeDoorSystem ()){
        printf("System initialization failed. Exiting.\n");
        LED_status_door_error();
        return EXIT_FAILURE;
    } else {
        printf("System initialized successfully.\n");
        // indicate ready: low-duty green steady
        LED_set_green_steady(true, 30);
    }

    // Start door UDP client reporting (heartbeat + notifications)
    if (!door_udp_init(hub_ip, 12345, module_id,
                       DOOR_REPORT_NOTIFICATION | DOOR_REPORT_HEARTBEAT,
                       1000)) {
        fprintf(stderr, "WARNING: door_udp_init failed, running without door reporting.\n");
    } else {
        door_udp_running = true;
    }
    // Initialize Discord
    if (!discordStart()) {
        fprintf(stderr, "Failed to initialize Discord\n");
        return 1;
    }
    hub_udp_set_webhook_url("https://discord.com/api/webhooks/1444219627461673080/rrr5SoaN1RpNC_PGoIH_mFWFV8fB4PosUd6qGC24M3zfg6nsDnvXAhyTxtr5qDiZOJy2");

    //start UDP listener
    if (!hub_udp_init(8888)) {
        fprintf(stderr, "Failed to initialize hub UDP\n");
        return 1;
    }
        // Start webhook reporter if provided via argv[3] or environment
        const char *webhook_url = (argc > 3) ? argv[3] : getenv("HUB_WEBHOOK_URL");
        bool webhook_running = false;
        if (webhook_url) {
            if (!hub_webhook_init(webhook_url)) {
                fprintf(stderr, "WARNING: webhook init failed, continuing without webhook.\n");
            } else {
                webhook_running = true;
            }
        }

    // ----------------------- UDP Communication Setup -----------------------
        if (!hub_udp_init(12345)) {
        fprintf(stderr, "Failed to start hub UDP listener\n");
        return 1;
    }

    printf("Hub listening on UDP port 12345...\n");
    // -------------------------EG door control loop ------------------------

       // Example: simple CLI loop where user can ask for status.
    while (1) {
        char cmd[64];
        printf("hub> ");
        if (!fgets(cmd, sizeof(cmd), stdin)) break;

        if (cmd[0] == 'q') break;

        if (cmd[0] == 's') {
            // status D1
            char id[16];
            if (sscanf(cmd, "s %15s", id) == 1) {
                HubDoorStatus st;
                if (hub_udp_get_status(id, &st)) {
                    printf("Status %s: D0=%s,%s D1=%s,%s lastHB=%lldms\n",
                           st.module_id,
                           st.d0_open   ? "OPEN" : "CLOSED",
                           st.d0_locked ? "LOCKED" : "UNLOCKED",
                           st.d1_open   ? "OPEN" : "CLOSED",
                           st.d1_locked ? "LOCKED" : "UNLOCKED",
                           st.last_heartbeat_ms);
                        // indicate hub command success briefly
                        LED_hub_command_success();
                        // also notify webhook (non-blocking)
                        char buf[256];
                        snprintf(buf, sizeof(buf), "Hub: status %s: D0=%s,%s D1=%s,%s",
                                 st.module_id,
                                 st.d0_open   ? "OPEN" : "CLOSED",
                                 st.d0_locked ? "LOCKED" : "UNLOCKED",
                                 st.d1_open   ? "OPEN" : "CLOSED",
                                 st.d1_locked ? "LOCKED" : "UNLOCKED");
                        hub_webhook_send(buf);
                } else {
                        LED_hub_command_failure();
                    printf("No status for %s yet.\n", id);
                        hub_webhook_send("Hub: status request failed (no status available)");
                }
            }
        }

        if (cmd[0] == 'h') {
            HubEvent events[20];
            int n = hub_udp_get_history(events, 20);
            for (int i = 0; i < n; i++) {
                printf("[%lld] %s: %s\n",
                       events[i].timestamp_ms,
                       events[i].module_id,
                       events[i].line);
            }
            if (n > 0) {
                LED_hub_command_success();
                    hub_webhook_send("Hub: history retrieved");
            } else {
                LED_hub_command_failure();
                    hub_webhook_send("Hub: history empty");
            }
        }
    }

    hub_udp_shutdown();

    if (door_udp_running) {
        door_udp_close();
    }
        if (webhook_running) {
            hub_webhook_shutdown();
        }

    return 0;

    // ----------------------- END OF EG Door Control Loop -----------------------
    
    int control_number;
    
    Door_t door1 = { .state = UNKNOWN };
        /* Use fgets + strtol for robust interactive input. If stdin is closed
         * or redirected, try opening /dev/tty (controlling terminal) so the
         * user can still interact. If neither is available we exit. */
        char line[128];
        FILE *in = stdin;
        bool opened_tty = false;

        if (!isatty(fileno(stdin))) {
            in = fopen("/dev/tty", "r");
            if (in) {
                opened_tty = true;
                printf("Reading interactive input from /dev/tty.\n");
            } else {
                /* Could not open /dev/tty; we'll fall back to stdin and detect EOF. */
                fprintf(stderr, "Warning: stdin is not a TTY and /dev/tty could not be opened: %s\n", strerror(errno));
                in = stdin;
            }
        }

        do {
            printf("Enter a number to do a task:\n1: Lock Door\n2: Unlock Door\n3: Get Door Status\n4: Exit\n");
            fflush(stdout);

            if (!fgets(line, sizeof(line), in)) {
                if (feof(in)) {
                    printf("Input closed (EOF). Exiting.\n");
                } else {
                    printf("Error reading input: %s. Exiting.\n", strerror(errno));
                }
                break;
            }

            char *endptr = NULL;
            long val = strtol(line, &endptr, 10);
            if (endptr == line || (*endptr != '\n' && *endptr != '\0')) {
                printf("Invalid input; please enter a number 1-4.\n");
                continue;
            }

            control_number = (int)val;

            if (control_number < 1 || control_number > 4) {
                printf("Invalid control number.\n");
                continue;
            }

            if (control_number == 1) {
                door1 = lockDoor(&door1);
                continue;
            } else if (control_number == 2) {
                door1 = unlockDoor(&door1);
                continue;
            } else if (control_number == 3) {
                door1 = get_door_status(&door1);
            }

        } while (control_number != 4);

        if (opened_tty && in != stdin) fclose(in);

    printf("Exiting door system.\n");
    return 0;
}
