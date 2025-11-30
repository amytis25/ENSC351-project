// door_udp_client.h
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

// New: initialize with separate ports for notifications and heartbeats.
// If notif_port == hb_port, behavior is identical to the old API.
bool door_udp_init2(const char *host_ip, uint16_t notif_port, uint16_t hb_port,
                    const char *module_id,
                    DoorReportMode mode,
                    int heartbeat_period_ms);

void door_udp_update(bool d0_open, bool d0_locked,
                     bool d1_open, bool d1_locked);

void door_udp_close(void);