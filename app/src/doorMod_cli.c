/*
 * doorMod_cli.c
 * Small CLI wrapper to run door module logic as a standalone executable.
 * Usage: ./doorMod_cli [MODULE_ID]
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "doorMod.h"
#include "hal/door_udp.h"
#include "hal/led.h"
#include "hal/led_worker.h"

#define DEBUG   // comment this out if you don't want CLI debug prints

int main(int argc, char *argv[])
{
    // Module ID from argv (optional)
    const char *module_id = (argc > 1) ? argv[1] : "D1";
    // Hub IP (adjust as needed)
    const char *hub_ip    = "192.168.8.108";
    bool reporting_running = false;

    // ---- Hardware / app init ----
    if (!initializeDoorSystem()) {
        fprintf(stderr, "Failed to initialize door system\n");
        return EXIT_FAILURE;
    }

    // ---- Start LED worker thread ----
    if (!LED_worker_init()) {
        fprintf(stderr, "Warning: LED_worker_init failed (continuing)\n");
    }

    // ---- Open the controlling terminal directly for CLI input ----
    FILE *tty = fopen("/dev/tty", "r");
    if (!tty) {
        perror("fopen(/dev/tty)");
        fprintf(stderr,
                "No interactive terminal available; CLI cannot run.\n");
        // you could return here, but we’ll just run headless
        // return EXIT_FAILURE;
    }

    // ---- Start UDP reporting (notifications + heartbeat) ----
    if (!door_reporting_start(hub_ip, 12345, 12346, module_id, 1000)) {
        fprintf(stderr,
                "WARNING: reporting init failed; continuing without UDP reporting\n");
        LED_status_network_error();   // immediate pattern (not queued)
    } else {
        reporting_running = true;
    }

    // Initial door state
    Door_t door = { .state = UNKNOWN };

    // ---- CLI loop ----
    printf("doorMod CLI started. Commands: l(lock), u(unlock), s(status), q(quit)\n");
    fflush(stdout);

    char line[128];

    while (tty) {
        printf("door> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), tty)) {
            perror("fgets(/dev/tty)");
            printf("\nEOF or error on /dev/tty, exiting CLI.\n");
            break;
        }

#ifdef DEBUG
        printf("DEBUG: raw line = '");
        for (size_t i = 0; i < strlen(line); i++) {
            unsigned char ch = (unsigned char)line[i];
            if (ch >= 32 && ch < 127)
                printf("%c", ch);
            else
                printf("\\x%02X", ch);
        }
        printf("'\n");
        fflush(stdout);
#endif

        // Strip trailing newline / carriage return
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[len - 1] = '\0';
            len--;
        }

        // Skip leading whitespace
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0') {
#ifdef DEBUG
            printf("DEBUG: empty line after trimming\n");
            fflush(stdout);
#endif
            continue;
        }

#ifdef DEBUG
        printf("DEBUG: trimmed command = '%s'\n", p);
        fflush(stdout);
#endif

        // First-char shortcuts (case-insensitive)
        char c = (char)tolower((unsigned char)p[0]);
        if (c == 'q') {
#ifdef DEBUG
            printf("DEBUG: quit command\n");
            fflush(stdout);
#endif
            break;
        }

        if (c == 'l') {
#ifdef DEBUG
            printf("DEBUG: calling lockDoor()\n");
            fflush(stdout);
#endif
            door = lockDoor(&door);
#ifdef DEBUG
            printf("DEBUG: returned from lockDoor(), state=%d\n", door.state);
            fflush(stdout);
#endif
            continue;
        }

        if (c == 'u') {
#ifdef DEBUG
            printf("DEBUG: calling unlockDoor()\n");
            fflush(stdout);
#endif
            door = unlockDoor(&door);
#ifdef DEBUG
            printf("DEBUG: returned from unlockDoor(), state=%d\n", door.state);
            fflush(stdout);
#endif
            continue;
        }

        if (c == 's') {
#ifdef DEBUG
            printf("DEBUG: calling get_door_status()\n");
            fflush(stdout);
#endif
            door = get_door_status(&door);
#ifdef DEBUG
            printf("DEBUG: returned from get_door_status(), state=%d\n", door.state);
            fflush(stdout);
#endif
            continue;
        }

        // Full-word commands (case-insensitive)
        if (strncasecmp(p, "lock",   4) == 0) { door = lockDoor(&door);        continue; }
        if (strncasecmp(p, "unlock", 6) == 0) { door = unlockDoor(&door);      continue; }
        if (strncasecmp(p, "status", 6) == 0) { door = get_door_status(&door); continue; }

        printf("Unknown command: '%s'\n", p);
        fflush(stdout);
    }

    if (tty) {
        fclose(tty);
    }

    // ---- Cleanup ----
    if (reporting_running) {
        door_reporting_stop();
    }

    LED_worker_shutdown();

    printf("Exiting doorMod CLI\n");
    return 0;
}