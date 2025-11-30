/*
 * doorMod_cli.c
 * Small CLI wrapper to run door module logic as a standalone executable.
 * Usage: ./doorMod_cli [MODULE_ID] [HUB_IP]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "doorMod.h"
#include "hal/door_udp_client.h"

int main(int argc, char *argv[])
{
    const char *module_id = (argc > 1) ? argv[1] : "D1";
    const char *hub_ip    = (argc > 2) ? argv[2] : "192.168.7.10";
    bool door_udp_running = false;

    if (!initializeDoorSystem()) {
        fprintf(stderr, "Failed to initialize door system\n");
        return EXIT_FAILURE;
    }

    // Try to start UDP reporting (best-effort)
    if (!door_udp_init(hub_ip, 12345, module_id,
                       DOOR_REPORT_NOTIFICATION | DOOR_REPORT_HEARTBEAT,
                       1000)) {
        fprintf(stderr, "WARNING: door_udp_init failed; continuing without UDP reporting\n");
    } else {
        door_udp_running = true;
    }

    Door_t door = { .state = UNKNOWN };

    printf("doorMod CLI started. Commands: l(lock), u(unlock), s(status), q(quit)\n");
    char line[128];

    while (1) {
        printf("door> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        if (line[0] == '\n' || line[0] == '\0') continue;

        if (line[0] == 'q') break;
        if (line[0] == 'l') {
            door = lockDoor(&door);
            continue;
        }
        if (line[0] == 'u') {
            door = unlockDoor(&door);
            continue;
        }
        if (line[0] == 's') {
            door = get_door_status(&door);
            continue;
        }

        // support textual commands
        if (strncmp(line, "lock", 4) == 0) { door = lockDoor(&door); continue; }
        if (strncmp(line, "unlock", 6) == 0) { door = unlockDoor(&door); continue; }
        if (strncmp(line, "status", 6) == 0) { door = get_door_status(&door); continue; }

        printf("Unknown command\n");
    }

    if (door_udp_running) door_udp_close();

    printf("Exiting doorMod CLI\n");
    return 0;
}
