// led.c
// ENSC 351 Fall 2025
// LED control functions for BeagleY-AI

#include "hal/led.h"
#include "hal/timing.h"
#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>

// from led guide 
// Helper function to write to a file
bool writeToFile(const char* filename, const char* value) {
    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        fprintf(stderr, "Error opening file '%s': ", filename);
        perror(NULL);
        return false;
    }
    
    if (fprintf(file, "%s", value) < 0) {
        fprintf(stderr, "Error writing to file '%s': ", filename);
        perror(NULL);
        fclose(file);
        return false;
    }
    
    fclose(file);
    return true;
}

// Green LED Functions
bool GreenLed_turnOn() {
    // Set trigger to none and brightness to 1
    if (!writeToFile(GREEN_LED_TRIGGER_FILE, "none")) {
        return false;
    }
    return writeToFile(GREEN_LED_BRIGHTNESS_FILE, "1");
}

bool GreenLed_turnOff() {
    // Set trigger to none and brightness to 0
    if (!writeToFile(GREEN_LED_TRIGGER_FILE, "none")) {
        return false;
    }
    return writeToFile(GREEN_LED_BRIGHTNESS_FILE, "0");
}

bool GreenLed_flash(long long delayInMs, int numRepeat) {
    // Set trigger to heartbeat for flashing effect
    if (!writeToFile(GREEN_LED_TRIGGER_FILE, "heartbeat")) {
        return false;
    }

    // Flash the LED the specified number of times
    for (int i = 0; i < numRepeat; i++) {
        // Turn on the LED
        if (!GreenLed_turnOn()) {
            return false;
        }

        // Sleep for the specified delay
         sleepForMs(delayInMs);

        // Turn off the LED
        if (!GreenLed_turnOff()) {
            return false;
        }

        // Sleep for the specified delay before the next flash
        sleepForMs(delayInMs);
    }

    return true;
}

void GreenLed_cleanup() {
    // Turn off the LED and reset trigger
    writeToFile(GREEN_LED_TRIGGER_FILE, "none");
    writeToFile(GREEN_LED_BRIGHTNESS_FILE, "0");
}

// Red LED Functions
bool RedLed_turnOn() {
    // Set trigger to none and brightness to 1
    if (!writeToFile(RED_LED_TRIGGER_FILE, "none")) {
        return false;
    }
    return writeToFile(RED_LED_BRIGHTNESS_FILE, "1");
}

bool RedLed_turnOff() {
    // Set trigger to none and brightness to 0
    if (!writeToFile(RED_LED_TRIGGER_FILE, "none")) {
        return false;
    }
    return writeToFile(RED_LED_BRIGHTNESS_FILE, "0");
}

bool RedLed_flash(long long delayInMs, int numRepeat) {
    // Set trigger to timer for flashing effect
    if (!writeToFile(RED_LED_TRIGGER_FILE, "timer")) {
        return false;
    }

    // Flash the LED the specified number of times
    for (int i = 0; i < numRepeat; i++) {
        // Turn on the LED
        if (!RedLed_turnOn()) {
            return false;
        }

        // Sleep for the specified delay
        sleepForMs(delayInMs);

        // Turn off the LED
        if (!RedLed_turnOff()) {
            return false;
        }

        // Sleep for the specified delay before the next flash
        sleepForMs(delayInMs);
    }

    return true;
}

void RedLed_cleanup() {
    // Turn off the LED and reset trigger
    writeToFile(RED_LED_TRIGGER_FILE, "none");
    writeToFile(RED_LED_BRIGHTNESS_FILE, "0");
}

