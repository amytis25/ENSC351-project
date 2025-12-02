#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>
#include "hal/HC-SR04.h"
#include "hal/GPIO.h"
#include "hal/timing.h"
#include "hal/StepperMotor.h"
#include "hal/led.h"
#include "hal/led_worker.h"
#include "doorMod.h"
#include "hal/door_udp.h"
/* app handler init prototype */
extern bool app_udp_handler_init(void);
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#define DEBUG
// forward declaration for helper used below
static void update_last_known_state(const Door_t *door);

// Small helper to map Door_t -> UDP booleans
static void report_door_state_udp(Door_t *door)
{
    // Map hardware to protocol channels:
    // D0 -> door open/close sensor
    // D1 -> lock state (locked/unlocked)
    if (!door) return;

    bool d0_open = false;
    bool d0_locked = false; // unused for door sensor
    bool d1_open = false;   // unused for lock
    bool d1_locked = false;

    // Door open/close from ultrasonic distance
    long long distance = get_distance();
    if (distance == -1) {
        // sensor error: don't send
        return;
    }
    // Threshold: distance >= 10 means open (matches existing logic)
    d0_open = (distance >= DOOR_CLOSED_THRESHOLD_CM);

    // Lock state from stepper position: 180 = locked
    d1_locked = (StepperMotor_GetPosition() == 180);

    // Send mapping: D0 is door sensor; D1 is lock state
    door_udp_update(d0_open, d0_locked, d1_open, d1_locked);

    // Update last-known state/time for heartbeat using existing Door_t semantics
    update_last_known_state(door);
}

// forward declaration for helper used below
static void update_last_known_state(const Door_t *door);

// --- Heartbeat & reporting support ---
static pthread_t __heartbeat_thread;
static int __heartbeat_running = 0;
static char *__report_module_id = NULL;
static char __report_hub_ip[64];
static uint16_t __heartbeat_port = 0;
static int __heartbeat_interval_ms = 1000;
static pthread_mutex_t __door_state_lock = PTHREAD_MUTEX_INITIALIZER;
static Door_t __last_known_door = { .state = UNKNOWN };
static long long __last_report_time_ms = 0;

static void update_last_known_state(const Door_t *door)
{
    pthread_mutex_lock(&__door_state_lock);
    if (door) __last_known_door = *door;
    __last_report_time_ms = getTimeInMs();
    pthread_mutex_unlock(&__door_state_lock);
}

static void *heartbeat_worker(void *arg)
{
    (void)arg;
    fprintf(stderr, "[heartbeat_worker] HAL heartbeat thread started - periodically calling door_udp_update()\n");
    // This thread calls door_udp_update() periodically to trigger
    // the HAL's heartbeat mechanism. The HAL handles the actual
    // HEARTBEAT packet sending via door_udp.c when conditions are met.
    
    while (__heartbeat_running) {
        sleepForMs(__heartbeat_interval_ms);
        
        // Read current hardware state directly (don't rely on stale cached state)
        long long distance = get_distance();
        uint16_t stepper_pos = StepperMotor_GetPosition();
        
        // Map to UDP booleans:
        // D0 = door open/close (from ultrasonic), D1 = lock state (from stepper)
        bool d0_open = (distance >= DOOR_CLOSED_THRESHOLD_CM);      // Door open if distance >= 10cm
        bool d0_locked = false;               // unused
        bool d1_open = false;                 // unused
        bool d1_locked = (stepper_pos == STEPPER_LOCKED_POSITION); // Lock is locked at 180 degrees
        
        // Call HAL's update function to send heartbeat/notifications
        door_udp_update(d0_open, d0_locked, d1_open, d1_locked);
    }
    
    return NULL;
}

bool door_reporting_start(const char *hub_ip, uint16_t report_port, uint16_t heartbeat_port,
                          const char *module_id, int heartbeat_ms)
{
    fprintf(stderr, "[door_reporting_start] Called with module_id='%s'\n", module_id);
    if (!hub_ip || !module_id) return false;

    // Start notification + heartbeat reporting using HAL helper
    // Enable BOTH modes so the HAL sends notifications on state change AND periodic heartbeats
    int mode = DOOR_REPORT_NOTIFICATION | DOOR_REPORT_HEARTBEAT;
    fprintf(stderr, "[door_reporting_start] Enabling HAL heartbeat mode (NOTIFICATION | HEARTBEAT)\n");
    fprintf(stderr, "[door_reporting_start] About to call door_udp_init2 with module_id='%s'\n", module_id);
    if (!door_udp_init2(hub_ip, report_port, heartbeat_port, module_id, mode, heartbeat_ms)) {
        // still allow heartbeat thread to run if desired
        fprintf(stderr, "[door_reporting_start] door_udp_init2 failed\n");
    }

    // configure heartbeat thread params
    strncpy(__report_hub_ip, hub_ip, sizeof(__report_hub_ip)-1);
    __report_hub_ip[sizeof(__report_hub_ip)-1] = '\0';
    __heartbeat_port = heartbeat_port;
    __heartbeat_interval_ms = (heartbeat_ms > 0) ? heartbeat_ms : 1000;
    __report_module_id = strdup(module_id);

    __heartbeat_running = 1;
    if (pthread_create(&__heartbeat_thread, NULL, heartbeat_worker, NULL) != 0) {
        __heartbeat_running = 0;
        free(__report_module_id);
        __report_module_id = NULL;
        return false;
    }

    // initialize last report time
    pthread_mutex_lock(&__door_state_lock);
    __last_report_time_ms = getTimeInMs();
    pthread_mutex_unlock(&__door_state_lock);

    return true;
}

void door_reporting_stop(void)
{
    // stop heartbeat
    __heartbeat_running = 0;
    pthread_join(__heartbeat_thread, NULL);
    
    if (__report_module_id) {
        free(__report_module_id);
        __report_module_id = NULL;
    }
    // stop notification reporting
    door_udp_close();
}


// Initialize the door system
bool initializeDoorSystem (){
    if (!init_hc_sr04 () || !StepperMotor_Init () ) {
        printf("Failed to initialize door module.\n");
        return false;
    }

    // Initialize LEDs (best-effort)
    if (!LED_init()) {
        fprintf(stderr, "Warning: LED_init failed (continuing)\n");
    }

    // Register app UDP command handler so the HAL transport forwards COMMANDs here
    app_udp_handler_init();

    return true;
}

long long avgDistanceSample (void){
    long long totalDistance = 0;
    int sample_count = 0;
    long long start_sampling_time = getTimeInMs();
    while ((getTimeInMs() - start_sampling_time) < 100){
        long long distance = get_distance();
        if (distance != -1){
            totalDistance += distance;
            sample_count++;
        }
        sleepForMs(5); // Small delay between samples
    }
    if (sample_count == 0){
        return -1; // Indicate error if no valid samples
    }

    return totalDistance / sample_count;
}


// Lock the door
Door_t lockDoor (Door_t *door){
    long long dist = avgDistanceSample();
    if (StepperMotor_GetPosition() == 180){
        printf("Door is already locked.\n");
    } else {
        if (dist >= DOOR_CLOSED_THRESHOLD_CM) {
            printf("Door is open, cannot lock.\n");
            door->state = OPEN;
            LED_enqueue_lock_failure();
        }
        else if (dist == -1) {
            printf("Ultrasonic sensor error, cannot lock door.\n");
            door->state = UNKNOWN;
            LED_enqueue_status_door_error();
        }
        else if (dist < DOOR_CLOSED_THRESHOLD_CM) {
            if (StepperMotor_Rotate(180)){
                printf("Door locked successfully.\n");
                // Update door state
                door->state = LOCKED;
                LED_enqueue_lock_success();
            } else {
                printf("Failed to lock the door.\n");
                LED_enqueue_lock_failure();
            }
        } else {
            printf("Failed to lock the door.\n");
            LED_enqueue_lock_failure();
        }
    }
    // Report new state to hub (if UDP initialized)
    report_door_state_udp(door);
    return *door;

}

// Unlock the door
Door_t unlockDoor (Door_t *door){
    if (StepperMotor_GetPosition() == 0){
        printf("Door is already unlocked.\n");
    } else {
        long long distance = get_distance();

        /* If our last-known state indicates the door was previously locked
           (for example from a recent heartbeat or event), allow unlocking
           without re-checking the ultrasonic sensor. This helps when the
           sensor is noisy or briefly unreliable right after a lock change. */
        pthread_mutex_lock(&__door_state_lock);
        bool last_was_locked = (__last_known_door.state == LOCKED);
        pthread_mutex_unlock(&__door_state_lock);

        if (last_was_locked || StepperMotor_GetPosition() == STEPPER_LOCKED_POSITION) {
            // Attempt unlock without checking distance
            if (StepperMotor_Rotate(STEPPER_UNLOCKED_POSITION)) {
                printf("Door unlocked successfully.\n");
                door->state = UNLOCKED;
                LED_enqueue_unlock_success();
            } else {
                printf("Failed to unlock the door.\n");
                LED_enqueue_unlock_failure();
            }
        }
        else if (distance >= DOOR_CLOSED_THRESHOLD_CM) {
            printf("Door is open, cannot unlock.\n");
            door->state = OPEN;
            LED_enqueue_unlock_failure();
        }
        else if (!last_was_locked) {
            printf("Door is already unlocked.\n");
            // Update door state
            door->state = UNLOCKED;
            #ifdef DEBUG
            printf("LED activating.\n");
            #endif
            LED_enqueue_unlock_success();
            #ifdef DEBUG
            printf("LED success.\n");
            #endif

        } else {
            printf("Failed to unlock the door.\n");
            LED_enqueue_unlock_failure();
        }
    }
    // Report new state to hub
    report_door_state_udp(door);
    return *door;

}

// Get the current status of the door
Door_t get_door_status (Door_t *door){
    long long distance = get_distance();
    if (StepperMotor_GetPosition() == STEPPER_LOCKED_POSITION){
        door->state = LOCKED;
        printf("Door is LOCKED.\n");
    } 
    else if (distance < DOOR_CLOSED_THRESHOLD_CM && distance != -1) {
        door->state = UNLOCKED;
        printf("Door is CLOSED and UNLOCKED.\n");
    }
    else if (distance == -1) {
        printf("Error reading distance from ultrasonic sensor.\n");
        LED_enqueue_status_door_error();
        // Report UNKNOWN (helper will skip sending) and return
        report_door_state_udp(door);
        return *door;
    }
    else if (distance >= DOOR_CLOSED_THRESHOLD_CM) {
        door->state = OPEN;
        printf("Door is OPEN.\n");
    } 
    else {        
        door->state = UNKNOWN; // Assuming any other position means closed but not locked
        printf("Door is in an UNKNOWN state.\n");
    }
    
    // Report current state to hub
    report_door_state_udp(door);
    return *door;

}
void doorMod_cleanup(void){
    if (StepperMotor_GetPosition()!= 0){
        // Unlock door on cleanup
        StepperMotor_Rotate(180);
    }
    // Stop reporting
    door_reporting_stop();

    // Shutdown LED worker
    LED_worker_shutdown();
    // Additional cleanup as needed
}
