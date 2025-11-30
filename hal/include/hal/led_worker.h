// led_worker.h - non-blocking LED task queue
#pragma once
#include <stdbool.h>

// Initialize the LED worker. Returns true on success.
bool LED_worker_init(void);

// Shutdown the LED worker (blocks until worker exits).
void LED_worker_shutdown(void);

// Enqueue tasks (non-blocking)
void LED_enqueue_blink_red_n(int flashes, int freqHz, int dutyPercent);
void LED_enqueue_hub_command_success(void);
void LED_enqueue_status_network_error(void);
void LED_enqueue_hub_command_failure(void);
void LED_enqueue_lock_success(void);
void LED_enqueue_lock_failure(void);
void LED_enqueue_unlock_success(void);
void LED_enqueue_unlock_failure(void);
void LED_enqueue_status_door_error(void);
