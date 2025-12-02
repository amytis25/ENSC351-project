// hub_udp.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <netinet/in.h>

#define HUB_PORT_NOTIF 12345
#define HUB_PORT_HB    12346

#define HUB_MAX_DOORS    8
#define HUB_MAX_HISTORY  256
#define HUB_MODULE_ID_LEN 16
#define HUB_LINE_LEN     256

typedef struct {
    char module_id[HUB_MODULE_ID_LEN];   // e.g., "D1"
    bool known;
    bool offline;  // true if heartbeat not received for > 10 seconds

    bool d0_open;
    bool d0_locked;
    bool d1_open;
    bool d1_locked;

    long long last_heartbeat_ms;
    long long last_event_ms;
    long long last_online_ms;  // timestamp when module went offline (or 0 if online)

    char last_heartbeat_line[HUB_LINE_LEN];
    // Last known source address for this module (useful for forwarding commands)
    int has_last_addr;
    struct sockaddr_in last_addr;
    // Last FEEDBACK seen from this module (for ACK matching)
    long long last_feedback_ms;
    char last_feedback_target[32];
    char last_feedback_action[32];
    int last_feedback_cmdid;
} HubDoorStatus;

typedef struct {
    long long timestamp_ms;
    char module_id[HUB_MODULE_ID_LEN];
    char line[HUB_LINE_LEN];
} HubEvent;

/**
 * Set the Discord webhook URL for alerts
 * The webhook URL (can be NULL to disable alerts)
 */
void hub_udp_set_webhook_url(const char *url);


// Start UDP listener thread on two ports. If listen_port2 == 0, only
// listen on the first port. Returns true on success.
bool hub_udp_init(uint16_t listen_port1, uint16_t listen_port2);

// Stop thread and close socket.
void hub_udp_shutdown(void);

// Get status for a given door ID ("D1", "D2", "D3").
// Returns true if that module is known and fills out *out.
bool hub_udp_get_status(const char *module_id, HubDoorStatus *out);

// Copy up to max_events most recent events into out[].
// Returns number of events copied (<= max_events).
int hub_udp_get_history(HubEvent *out, int max_events);

// Send a command to a known module (returns true on send success)
bool hub_udp_send_command(const char *module_id, const char *target, const char *action);
