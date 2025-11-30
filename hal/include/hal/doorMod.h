#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>
#include "hal/HC-SR04.h"
#include "hal/GPIO.h"
#include "hal/timing.h"
#include "hal/StepperMotor.h"

typedef enum {
    LOCKED,
    UNLOCKED,
    OPEN,
    UNKNOWN
} DoorState_t;

typedef struct {
    DoorState_t state;
    int id;
    char door1_name[50];
    // Additional door-related fields can be added here
} Door_t;

// Initialize the door system
bool initializeDoorSystem ();

// Lock the door
Door_t lockDoor (Door_t *door);

// Unlock the door
Door_t unlockDoor (Door_t *door);

// Get the current status of the door
Door_t get_door_status (Door_t *door);

// Reporting control (thin wrappers).
// Start reporting: `report_port` used for notification messages, `heartbeat_port`
// used for periodic heartbeat UDP packets. `heartbeat_ms` is interval in ms.
bool door_reporting_start(const char *hub_ip, uint16_t report_port, uint16_t heartbeat_port,
                         const char *module_id, int heartbeat_ms);
void door_reporting_stop(void);
