/* HC-SR04 driver implementation using project's GPIO helpers and timing utilities */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "hal/HC-SR04.h"
#include "hal/GPIO.h"
#include "hal/timing.h"

/* Use macros from header for pins */

bool init_hc_sr04(){
    if(!export_pin(TRIG_PIN, "out")) return false;
    if(!export_pin(ECHO_PIN, "in")) return false;
    if(!set_pin_direction(TRIG_PIN, "out")) return false;
    if(!set_pin_direction(ECHO_PIN, "in")) return false;
    if(!write_pin_value(TRIG_PIN, 0)) return false;
    return true;
}


long long get_distance(){

    if(!write_pin_value(TRIG_PIN, 0)) return -1;
    sleepForUs(2); /* ~2us nominal  */
    if(!write_pin_value(TRIG_PIN, 1)) return -1;
    sleepForUs(10); /* ~10us nominal */
    if(!write_pin_value(TRIG_PIN, 0)) return -1;

    const long long timeout_ms = 60; /* 60 ms timeout */

    long long wait_start = getTimeInMs();

    /* Wait for rising edge */
    while(true){
        int v = read_pin_value(ECHO_PIN);
        if(v < 0) {
            perror("Failed to read echo pin value");
            return -1;
        }
        if(v == 1) {
            break;
        }
        if((getTimeInMs() - wait_start) > timeout_ms) {
            perror("Timeout waiting for echo pin to go high");
            return -1; /* timeout */
        }
        sleepForUs(10);
    }

    long long t_start = getTimeInMs();

    /* Wait for falling edge */
    while(true){
        int v = read_pin_value(ECHO_PIN);
        if(v < 0) {
            perror("Failed to read echo pin value");
            return -1;
        }
        if(v == 0) {
            break;
        }
        if((getTimeInMs() - t_start) > timeout_ms) {
            perror("Timeout waiting for echo pin to go low");
            return -1; /* timeout */
        }
        sleepForUs(10);
    }

    long long t_end = getTimeInMs();
    long long pulse_ms = t_end - t_start;

    /* Convert ms to microseconds for distance formula */
    long long pulse_us = pulse_ms * 1000LL;

    /* distance in cm = pulse_us / 58 */
    long long distance_cm = pulse_us * 0.01715; 
    return distance_cm;
}