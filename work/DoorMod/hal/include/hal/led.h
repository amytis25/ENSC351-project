// led.c
// ENSC 351 Fall 2025
// LED control functions for BeagleY-AI

#ifndef LED_H
#define LED_H
#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>


#define GREEN_LED_TRIGGER_FILE "/sys/class/leds/ACT/trigger"
#define GREEN_LED_BRIGHTNESS_FILE "/sys/class/leds/ACT/brightness"
#define RED_LED_TRIGGER_FILE "/sys/class/leds/PWR/trigger"
#define RED_LED_BRIGHTNESS_FILE "/sys/class/leds/PWR/brightness"

// write a string value to a file, return true if successful, false otherwise
bool writeToFile(const char* filename, const char* value);

// Green LED helper Functions
bool GreenLed_turnOn();
bool GreenLed_turnOff();

// Red LED helper Functions
bool RedLed_turnOn();
bool RedLed_turnOff();

// Flash the LED with specified delay (in ms) and number of repeats
bool GreenLed_flash(long long delayInMs, int numRepeat);
bool RedLed_flash(long long delayInMs, int numRepeat);

// Cleanup functions to turn off LEDs and reset triggers
void RedLed_cleanup();
void GreenLed_cleanup();



#endif