// led.h
// Functions to control the LEDs on door module and hub

#ifndef HAL_LED_H
#define HAL_LED_H

#include <stdbool.h>

// Default PWM pins used by the board. Change these if your wiring differs.
#define LED_GREEN_PIN "GPIO15"
#define LED_RED_PIN   "GPIO12"

// Initialization: export PWM pins for LEDs. Returns true on success.
bool LED_init(void);

// Shutdown: stop any worker and cleanup. Call at program exit when LED
// hardware is no longer required.
void LED_shutdown(void);

// Low-level helpers
// Set steady output on/off. `dutyPercent` is 0-100 (ignored if on==false).
void LED_set_green_steady(int dutyPercent);
void LED_set_red_steady(int dutyPercent);

// Blink helpers (blocking): blink `flashes` times at `freqHz` with `dutyPercent`.
void LED_blink_red_n(int freqHz, int dutyPercent);
void LED_blink_green_n(int freqHz, int dutyPercent);

// High-level sequences described in comments
void LED_lock_success_sequence(void);
void LED_lock_failure_sequence(void);
void LED_unlock_success_sequence(void);
void LED_unlock_failure_sequence(void);

void LED_hub_command_success(void);
void LED_hub_command_failure(void);

// Status indicators
void LED_status_door_error(void);     // red flash at 2Hz
void LED_status_network_error(void);  // green flash at 2Hz

#endif // HAL_LED_H
//led.h
//functions to control the LEDs on door module and hub