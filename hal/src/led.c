//led.c
//contains LED patterns for door module and hub using PWM
#include "hal/led.h"
#include "hal/PWM.h"
#include "hal/led_worker.h"

/* LED lock door
// green on during lock mechanism (5hz?)
// unsuccessful lock/unlock 5 red flashes @ 10Hz
// green off
// red flash at 2hz

// successful lock/unlock 2 red flashes @ 7Hz
// green on 
*/

/* LED unlock door 
// green off
// unsuccessful lock/unlock 5 red flashes @ 10Hz
// red flash at 2hz

// successful lock/unlock 2 red flashes @ 7Hz
*/

/* LED hub command
// 2 red flashes @ 7Hz when success 

// 5 red flashes @ 10Hz when unsuccessful
*/

// led.c
// Implementations for LED control using PWM

#define DEBUG 

#include "hal/led.h"
#include "hal/PWM.h"
#include "hal/timing.h"
#include <stdio.h>
#include <stdbool.h>

// Internal helper: blink a pin `flashes` times at `freqHz` with `dutyPercent`.
// This is a blocking helper (uses sleeps). Caller may run in a thread if non-blocking behavior is required.
static void blink_led(LEDt led, int flashes, int freqHz, int dutyPercent)
{
	if (flashes <= 0 || freqHz <= 0) return;

	fprintf(stderr, "[blink_led] Starting: %d flashes @ %dHz, duty=%d%%\n", flashes, freqHz, dutyPercent);

	// Enable PWM at requested frequency and duty
	if (!PWM_setFrequency(led, freqHz, dutyPercent)) {
		fprintf(stderr, "[blink_led] PWM_setFrequency failed\n");
		return;
	}

	// total time = flashes * period (each period is one on/off cycle)
	int period_ms = 1000 / freqHz;
	int total_ms = period_ms * flashes;
	fprintf(stderr, "[blink_led] Sleeping for %dms (period=%dms)\n", total_ms, period_ms);
	sleepForMs(total_ms);

	fprintf(stderr, "[blink_led] Disabling LED\n");
	// turn off
	PWM_disable(led);
}

// Initialization: export both PWM pins
bool LED_init(void)
{
	bool ok = true;
	if (!PWM_export()) {
		fprintf(stderr, "LED_init: PWM_export failed (continuing without hardware)\n");
		ok = false;
	}
	
	/* Start the LED worker regardless of PWM export status.
	   The worker will still enqueue and execute LED sequences;
	   if PWM hardware is unavailable, sequences execute with debug output. */
	if (!LED_worker_init()) {
		fprintf(stderr, "LED_init: Failed to start LED worker\n");
		ok = false;
	}

	return ok;
}

// Shutdown helper: ensure the worker is stopped. Call at program exit.
void LED_shutdown(void)
{
	LED_worker_shutdown();
}

// Low-level steady control
void LED_set_green_steady(bool on, int dutyPercent)
{
	if (!on) {
		PWM_disable(GREEN_LED);
		return;
	}
	// Use a high PWM frequency so the LED appears steady.
	if (!PWM_setFrequency(GREEN_LED, 1000, dutyPercent)) {
		fprintf(stderr, "LED_set_green_steady: failed to set PWM for %s\n", LED_GREEN_PIN);
	}
}

void LED_set_red_steady(bool on, int dutyPercent)
{
	if (!on) {
		PWM_disable(RED_LED);
		return;
	}
	if (!PWM_setFrequency(RED_LED, 1000, dutyPercent)) {
		fprintf(stderr, "LED_set_red_steady: failed to set PWM for %s\n", LED_RED_PIN);
	}
}

// Blink helpers (blocking)
void LED_blink_red_n(int flashes, int freqHz, int dutyPercent)
{
	blink_led(RED_LED, flashes, freqHz, dutyPercent);
}

void LED_blink_green_n(int flashes, int freqHz, int dutyPercent)
{
	blink_led(GREEN_LED, flashes, freqHz, dutyPercent);
}

// High-level sequences
void LED_lock_success_sequence(void)
{
	#ifdef DEBUG
	fprintf(stderr, "[LED] Lock SUCCESS: 2 red flashes @ 7Hz, then green steady\n");
	#endif
	// Ensure green is off before starting sequence
	PWM_disable(GREEN_LED);
	// 2 red flashes @ 7Hz then green on steady (lower duty)
	LED_blink_red_n(2, 7, 90);
	LED_set_green_steady(true, 30);
}

void LED_lock_failure_sequence(void)
{
	#ifdef DEBUG
	fprintf(stderr, "[LED] Lock FAILURE: 5 red flashes @ 10Hz, then red @ 2Hz continuous\n");
	#endif
	// Ensure green is off
	PWM_disable(GREEN_LED);
	// 5 red flashes @ 10Hz, then red flash at 2Hz (continuous)
	LED_blink_red_n(5, 10, 90);
	// slow red flash at 2Hz
	if (!PWM_setFrequency(RED_LED, 2, 50)) {
		PWM_disable(RED_LED);
	}
}

void LED_unlock_success_sequence(void)
{
	#ifdef DEBUG
	fprintf(stderr, "[LED] Unlock SUCCESS: 2 red flashes @ 7Hz, then green steady\n");
	#endif
	// Ensure green is off before starting sequence
	PWM_disable(GREEN_LED);
	// 2 red flashes @ 7Hz then green on steady (lower duty)
	LED_blink_red_n(2, 7, 90);
	LED_set_green_steady(true, 30);
}

void LED_unlock_failure_sequence(void)
{
	#ifdef DEBUG
	fprintf(stderr, "[LED] Unlock FAILURE: 5 red flashes @ 10Hz, then red @ 2Hz continuous\n");
	#endif
	// Ensure green is off
	PWM_disable(GREEN_LED);
	// 5 red flashes @ 10Hz, then red flash at 2Hz (continuous)
	LED_blink_red_n(5, 10, 90);
	// slow red flash at 2Hz
	if (!PWM_setFrequency(RED_LED, 2, 50)) {
		PWM_disable(RED_LED);
	}
}

void LED_hub_command_success(void)
{
	// 2 red flashes @ 7Hz
	LED_blink_red_n(2, 7, 90);
}

void LED_hub_command_failure(void)
{
	// 5 red flashes @ 10Hz
	LED_blink_red_n(5, 10, 90);
}

void LED_status_door_error(void)
{
	// red flash at 2Hz (continuous)
	if (!PWM_setFrequency(RED_LED, 2, 50)) {
		PWM_disable(RED_LED);
	}
}

void LED_status_network_error(void)
{
	// green flash at 2Hz (continuous)
	if (!PWM_setFrequency(GREEN_LED, 2, 50)) {
		PWM_disable(GREEN_LED);
	}
}
