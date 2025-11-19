/* HC-SR04 driver implementation using project's GPIO helpers and timing utilities */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "hal/HC-SR04.h"
#include "hal/GPIO.h"
#include "hal/timing.h"

/* Debug output control - set to 0 to disable debug printfs */
#define HC_SR04_DEBUG 1
#define HC_SR04_DEBUG_VERBOSE 0  /* Set to 1 for detailed per-read debug messages */

#if HC_SR04_DEBUG
    #define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(...) do {} while(0)
#endif

#if HC_SR04_DEBUG_VERBOSE
    #define DEBUG_VERBOSE(...) printf(__VA_ARGS__)
#else
    #define DEBUG_VERBOSE(...) do {} while(0)
#endif

bool init_hc_sr04(){
    DEBUG_PRINT("Initializing HC-SR04 sensor:\n");
    DEBUG_PRINT("Trigger pin: chip %d, line %d\n", TRIG_GPIOCHIP, TRIG_GPIO_LINE);
    DEBUG_PRINT("Echo pin: chip %d, line %d\n", ECHO_GPIOCHIP, ECHO_GPIO_LINE);
    
    /* Initialize trigger pin as output */
    DEBUG_PRINT("Setting up trigger pin...\n");
    if(!export_pin(TRIG_GPIOCHIP, TRIG_GPIO_LINE, "out")) {
        DEBUG_PRINT("Failed to initialize trigger pin (chip %d, line %d)\n", 
               TRIG_GPIOCHIP, TRIG_GPIO_LINE);
        return false;
    }
    
    /* Initialize echo pin as input */
    DEBUG_PRINT("Setting up echo pin...\n");
    if(!export_pin(ECHO_GPIOCHIP, ECHO_GPIO_LINE, "in")) {
        DEBUG_PRINT("Failed to initialize echo pin (chip %d, line %d)\n",
               ECHO_GPIOCHIP, ECHO_GPIO_LINE);
        return false;
    }

    /* Ensure trigger starts low */
    DEBUG_PRINT("Setting trigger initial state...\n");
    if(!write_pin_value(TRIG_GPIOCHIP, TRIG_GPIO_LINE, 0)) {
        DEBUG_PRINT("Failed to set trigger initial state\n");
        return false;
    }
    
    DEBUG_PRINT("HC-SR04 initialization successful\n");
    return true;
}


long long get_distance(){
    /* Trigger pulse sequence */
    if(!write_pin_value(TRIG_GPIOCHIP, TRIG_GPIO_LINE, 0)) {
        perror("Failed to set trigger low");
        return -1;
    }
    sleepForUs(2); /* At least 2us */

    if(!write_pin_value(TRIG_GPIOCHIP, TRIG_GPIO_LINE, 1)) {
        perror("Failed to set trigger high");
        return -1;
    }
    sleepForUs(10); /* At least 10us */

    if(!write_pin_value(TRIG_GPIOCHIP, TRIG_GPIO_LINE, 0)) {
        perror("Failed to set trigger low");
        return -1;
    }

    const long long timeout_us = 60000; /* 60 ms timeout */
    long long wait_start = getTimeInUs();

    /* Wait for rising edge */
    DEBUG_VERBOSE("Waiting for echo pin to go high...\n");
    while(true) {
        int v = read_pin_value(ECHO_GPIOCHIP, ECHO_GPIO_LINE);
        if(v < 0) {
            perror("Failed to read echo pin value");
            return -1;
        }
        if(v == 1) {
            DEBUG_VERBOSE("Echo pin went high!\n");
            break;
        }
        if((getTimeInUs() - wait_start) > timeout_us) {
            DEBUG_PRINT("Timeout waiting for echo pin to go high (pin value = %d)\n", v);
            printf("Timeout waiting for echo pin to go high\n");
            return -1;
        }
        sleepForUs(50); /* Poll every 50us for better resolution */
    }

    long long t_start = getTimeInUs();

    /* Wait for falling edge */
    DEBUG_VERBOSE("Waiting for echo pin to go low...\n");
    int read_count = 0;
    while(true) {
        int v = read_pin_value(ECHO_GPIOCHIP, ECHO_GPIO_LINE);
        if(v < 0) {
            perror("Failed to read echo pin value");
            return -1;
        }
        read_count++;
        if(read_count <= 5 || read_count % 10 == 0) {
            DEBUG_VERBOSE("Echo pin value = %d (read #%d)\n", v, read_count);
        }
        if(v == 0) {
            DEBUG_VERBOSE("Echo pin went low after %d reads!\n", read_count);
            break;
        }
        if((getTimeInUs() - t_start) > timeout_us) {
            DEBUG_VERBOSE("Timeout waiting for echo pin to go low (pin value = %d, reads = %d)\n", v, read_count);
            printf("Timeout waiting for echo pin to go low\n");
            return 10000;
        }
        sleepForUs(50); /* Poll every 50us */
    }

    long long t_end = getTimeInUs();
    long long pulse_us = t_end - t_start;

    /* Validate pulse width (typical range ~150us to ~30ms) */
    if (pulse_us < 100 || pulse_us > 60000) {
        DEBUG_PRINT("Discarding out-of-range pulse: %lld us\n", pulse_us);
        return -1;
    }

    /* distance_cm = pulse_us * 0.01716 */
    return (long long)(pulse_us * 0.01716);
}