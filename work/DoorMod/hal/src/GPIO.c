#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>
#include <string.h> // strcmp
#include <gpiod.h>
#include "hal/GPIO.h"
#include "hal/timing.h"



bool export_pin(int pin) {
    FILE *fp;

    // Export pin
    fp = fopen("/sys/class/gpio/export", "w");
    if (fp == NULL) {
        perror("Failed to open export for writing");
        exit(EXIT_FAILURE);
        return false;
    } else {
        fprintf(fp, "%d", pin);
        fclose(fp);
        return true;
    }
}
bool set_pin_direction(int pin, const char* direction) {
    FILE *fp;

    // Set pin direction
    char path[50];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    fp = fopen(path, "w");
    if (fp == NULL) {
        perror("Failed to open direction for writing");
        exit(EXIT_FAILURE);
        return false;
    } else {
        fprintf(fp, "%s", direction);
        fclose(fp);
        return true;
    }
}
 
bool write_pin_value(int pin, int value) {
    FILE *fp;

    // Write value to pin
    char path[50];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    fp = fopen(path, "w");
    if (fp == NULL) {
        perror("Failed to open value for writing");
        exit(EXIT_FAILURE);
        return false;
    } else {
        fprintf(fp, "%d", value);
        fclose(fp);
        return true;
    }
}

int read_pin_value(int pin) {
    FILE *fp;
    int value = -1;

    // Read value from pin
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }

    if (fscanf(fp, "%d", &value) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return value;
}





