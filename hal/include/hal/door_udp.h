// door_udp.h
// Lower level UDP handler (transport) for door communication.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DOOR_REPORT_NONE         = 0,
    DOOR_REPORT_NOTIFICATION = 1 << 0,
    DOOR_REPORT_HEARTBEAT    = 1 << 1
} DoorReportMode;

bool door_udp_init(const char *host_ip, uint16_t port,
                   const char *module_id,
                   DoorReportMode mode,
                   int heartbeat_period_ms);

// Initialize with separate ports for notifications and heartbeats.
// If notif_port == hb_port, behavior is identical to the old API.
bool door_udp_init2(const char *host_ip, uint16_t notif_port, uint16_t hb_port,
                    const char *module_id,
                    DoorReportMode mode,
                    int heartbeat_period_ms);

void door_udp_update(bool d0_open, bool d0_locked,
                     bool d1_open, bool d1_locked);

void door_udp_close(void);

/* Command handler callback: invoked when a COMMAND is received for this module.
 * module: module id in the incoming message (should match registered module)
 * cmdid: numeric command id
 * target/action: textual tokens from the COMMAND
 * ctx: user-provided context from register call
 */
typedef void (*DoorCmdHandler)(const char *module, int cmdid, const char *target, const char *action, void *ctx);

/* Register a command handler callback. Returns true on success. */
bool door_udp_register_command_handler(DoorCmdHandler handler, void *ctx);

/* Send a FEEDBACK message back to the hub (formatted by HAL transport).
 * Returns true if the send succeeded (socket open), false otherwise.
 */
bool door_udp_send_feedback(const char *module, int cmdid, const char *target, const char *action);
