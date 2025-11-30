#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>
#include "hal/HC-SR04.h"
#include "hal/GPIO.h"
#include "hal/timing.h"
#include "hal/StepperMotor.h"
#include "hal/led.h"
#include "hal/doorMod.h"
#include "hal/door_udp_client.h"
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

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
    d0_open = (distance >= 10);

    // Lock state from stepper position: 180 = locked
    d1_locked = (StepperMotor_GetPosition() == 180);

    // Send mapping: D0 is door sensor; D1 is lock state
    door_udp_update(d0_open, d0_locked, d1_open, d1_locked);

    // Update last-known state/time for heartbeat using existing Door_t semantics
    update_last_known_state(door);
}

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
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return NULL;

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(__heartbeat_port);
    if (inet_aton(__report_hub_ip, &dest.sin_addr) == 0) {
        close(sock);
        return NULL;
    }

    while (__heartbeat_running) {
        char msg[256];
        pthread_mutex_lock(&__door_state_lock);
        Door_t s = __last_known_door;
        long long last_ms = __last_report_time_ms;
        char *mid = __report_module_id;
        pthread_mutex_unlock(&__door_state_lock);

        const char *online = "ONLINE";
        const char *openclose = (s.state == OPEN) ? "OPEN" : "CLOSED";
        const char *lockstate = (s.state == LOCKED) ? "LOCK" : "UNLOCK";
        long long now = getTimeInMs();
        long long since_last = (last_ms == 0) ? -1 : (now - last_ms) / 1000; // seconds

        if (since_last < 0)
            snprintf(msg, sizeof(msg), "%s %s %s %s -1", mid, online, openclose, lockstate);
        else
            snprintf(msg, sizeof(msg), "%s %s %s %s %lld", mid, online, openclose, lockstate, since_last);

        sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&dest, sizeof(dest));

        // sleep for interval (ms)
        sleepForMs(__heartbeat_interval_ms);
    }

    close(sock);
    return NULL;
}

bool door_reporting_start(const char *hub_ip, uint16_t report_port, uint16_t heartbeat_port,
                          const char *module_id, int heartbeat_ms)
{
    if (!hub_ip || !module_id) return false;

    // Start notification and heartbeat reporting using HAL helper
    if (!door_udp_init2(hub_ip, report_port, heartbeat_port, module_id, DOOR_REPORT_NOTIFICATION | DOOR_REPORT_HEARTBEAT, heartbeat_ms)) {
        // still allow heartbeat thread to run if desired
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
    pthread_cond_t tmpcond;
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
        if (dist >= 10) {
            printf("Door is open, cannot lock.\n");
            door->state = OPEN;
            LED_lock_failure_sequence();
        }
        else if (dist == -1) {
            printf("Ultrasonic sensor error, cannot lock door.\n");
            door->state = UNKNOWN;
            LED_status_door_error();
        }
        else if (dist < 10) {
            if (StepperMotor_Rotate(180)){
                printf("Door locked successfully.\n");
                // Update door state
                door->state = LOCKED;
                LED_lock_success_sequence();
            } else {
                printf("Failed to lock the door.\n");
                LED_lock_failure_sequence();
            }
        } else {
            printf("Failed to lock the door.\n");
            LED_lock_failure_sequence();
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
        if (distance >= 5) {
            printf("Door is open, cannot unlock.\n");
            door->state = OPEN;
            LED_unlock_failure_sequence();
        }
        else if (StepperMotor_Rotate(180)) {
            printf("Door unlocked successfully.\n");
            // Update door state
            door->state = UNLOCKED;
            LED_unlock_success_sequence();
        } else {
            printf("Failed to unlock the door.\n");
            LED_unlock_failure_sequence();
        }
    }
    // Report new state to hub
    report_door_state_udp(door);
    return *door;

}

// Get the current status of the door
Door_t get_door_status (Door_t *door){
    long long distance = get_distance();
    if (StepperMotor_GetPosition() == 180){
        door->state = LOCKED;
        printf("Door is LOCKED.\n");
    } 
    else if (distance < 10 && distance != -1) {
        door->state = UNLOCKED;
        printf("Door is CLOSED and UNLOCKED.\n");
    }
    else if (distance == -1) {
        printf("Error reading distance from ultrasonic sensor.\n");
        LED_status_door_error();
        // Report UNKNOWN (helper will skip sending) and return
        report_door_state_udp(door);
        return *door;
    }
    else if (distance >= 10) {
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
