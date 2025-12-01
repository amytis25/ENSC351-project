/* app/include/doorMod.h
 * Public header for application-level door module APIs.
 * Keeps the surface small and HAL-agnostic so callers don't pull in
 * hardware headers. Implementations live in `app/src/`.
 */

#ifndef APP_DOORMOD_H
#define APP_DOORMOD_H

#include <stdbool.h>
#include <stdint.h>

/* Door state exposed to callers. Values are explicit for ABI stability. */
typedef enum {
    LOCKED = 0,
    UNLOCKED = 1,
    OPEN = 2,
    UNKNOWN = 3
} DoorState_t;

/* Minimal door status object exposed to consumers (UI, tests). */
typedef struct {
    DoorState_t state;
    int id;
} Door_t;

/* Initialize hardware and application subsystems required by the door
 * module. Returns true on success. Implementation may initialize HAL
 * drivers internally; callers should not rely on ordering. */
bool initializeDoorSystem(void);

/* Start/stop UDP reporting (notifications + heartbeat) at the application
 * level. `report_port` is used for notifications and `heartbeat_port` for
 * heartbeat packets. `heartbeat_ms` is heartbeat interval in milliseconds. */
bool door_reporting_start(const char *hub_ip, uint16_t report_port, uint16_t heartbeat_port,
                          const char *module_id, int heartbeat_ms);
void door_reporting_stop(void);

/* Synchronous door control APIs. Operate on a caller-provided Door_t and
 * return the updated state by value. */
Door_t lockDoor(Door_t *door);
Door_t unlockDoor(Door_t *door);
Door_t get_door_status(Door_t *door);

#endif /* APP_DOORMOD_H */
