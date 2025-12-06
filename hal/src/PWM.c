#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>
#include "hal/timing.h"
#include "hal/PWM.h"
#include <unistd.h>
#include <string.h>
#include <limits.h>

//#define DEBUG 

//#define DEBUG 

#define NANOSECONDS_IN_SECOND 1000000000

/* Define the LEDt instances here (the header declares them `extern`).
   This ensures a single definition is present for the whole program and
   avoids multiple-definition linker errors when the header is included
   from multiple compilation units. */
const LEDt RED_LED = {
    .duty_cycle = "/dev/hat/pwm/GPIO13/duty_cycle",
    .period     = "/dev/hat/pwm/GPIO13/period",
    .enable     = "/dev/hat/pwm/GPIO13/enable"
};
const LEDt GREEN_LED = {
    .duty_cycle = "/dev/hat/pwm/GPIO12/duty_cycle",
    .period     = "/dev/hat/pwm/GPIO12/period",
    .enable     = "/dev/hat/pwm/GPIO12/enable"
};

// Helper function to write to a file
bool PWM_export(void){
    // Ensure both GPIO15 and GPIO12 PWM sysfs entries exist. Try exporting any
    // pin whose enable file is not present yet.
    const char *pins[] = { "GPIO13", "GPIO12" };
    const char *enable_paths[] = { RED_LED.enable, GREEN_LED.enable };

    bool all_ok = true;
    for (size_t p = 0; p < sizeof(pins)/sizeof(pins[0]); ++p) {
        const char *path = enable_paths[p];
        if (access(path, F_OK) == 0) {
            continue; // already exists
        }

        // attempt to export this pin
        char cmd[128];
        int n = snprintf(cmd, sizeof(cmd), "beagle-pwm-export --pin %s", pins[p]);
        if (n < 0) {
            fprintf(stderr, "PWM_export: snprintf failed\n");
            all_ok = false;
            continue;
        }
        int rc = system(cmd);
        if (rc != 0) {
            fprintf(stderr, "PWM_export: beagle-pwm-export failed for %s (rc=%d)\n", pins[p], rc);
            all_ok = false;
            continue;
        }

        // Wait briefly for sysfs entries to appear
        bool found = false;
        for (int i = 0; i < 20; ++i) {
            if (access(path, F_OK) == 0) { found = true; break; }
            usleep(100000); // 100 ms
        }
        if (!found) {
            fprintf(stderr, "PWM_export: timeout waiting for %s\n", path);
            all_ok = false;
        }
    }

    return all_ok;
}
static bool writeToFile(const char* filename, const char* value) {
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

// PWM helper Functions
bool PWM_setDutyCycle(LEDt led, int dutyCycle){
    #ifdef DEBUG
    printf("Setting duty cycle to %d\n", dutyCycle);
    #endif
    //writeToFile(PWM_DUTY_CYCLE_FILE, "0"); // --- IGNORE ---
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d", dutyCycle);
    if (n < 0) return false;
    return writeToFile(led.duty_cycle, buf);
}

bool PWM_setPeriod(LEDt led, int period){
    #ifdef DEBUG
    printf("Setting period to %d\n", period);
    #endif
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d", period);
    if (n < 0) return false;
    return writeToFile(led.period, buf);
}


bool PWM_setFrequency(LEDt led, int Hz, int dutyCyclePercent)
{
    if (dutyCyclePercent < 0 || dutyCyclePercent > 100) {
        fprintf(stderr, "PWM_setFrequency: invalid duty cycle percentage\n");
        return false;
    }

    // Assignment spec: support 0 - 500 Hz
    if (Hz < 0)   Hz = 0;
    if (Hz > 500) Hz = 500;

    if (Hz == 0) {
        // 0 Hz -> LED off / stop PWM
        return PWM_disable(led);
    }

    // Use 64-bit to avoid any overflow/rounding surprises
    const unsigned long long NSEC = 1000000000ULL;
    unsigned long long period_ull = NSEC / (unsigned long long)Hz;
    unsigned long long duty_ull   = (period_ull * (unsigned long long)dutyCyclePercent) / 100ULL;

    // SAFEST ORDER:
    // 1) duty=0 -> disable (so period can shrink safely)
    // 2) period=new
    // 3) duty=desired
    // 4) enable
    
    // disable
    if (!PWM_disable(led)) return false;

    // cast to int because helpers take int
    if (!PWM_setPeriod(led, (int)period_ull)) return false;
    if (!PWM_setDutyCycle(led, (int)duty_ull)) return false;
    // now enable
    PWM_enable(led);
    return true;
}
bool PWM_enable(LEDt led){
    #ifdef DEBUG
    printf("Enabling PWM\n");
    #endif
    return writeToFile(led.enable, "1");
}

bool PWM_disable(LEDt led){
    #ifdef DEBUG
    printf("Disabling PWM\n");
    #endif
    return writeToFile(led.enable, "0");
}