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

#define NANOSECONDS_IN_SECOND 1000000000

// Helper function to write to a file
bool PWM_export(void){
    return PWM_export_pin(DEFAULT_PWM_PIN);
}
// Helper: build a sysfs path for a given pin and filename
static void build_path(char *out, size_t outlen, const char *pin, const char *filename)
{
    if (out == NULL || pin == NULL || filename == NULL) return;
    snprintf(out, outlen, PWM_SYSFS_PATH_FORMAT, pin, filename);
}

bool PWM_export_pin(const char* pin){
    if (pin == NULL) return false;
    char enable_path[PATH_MAX];
    build_path(enable_path, sizeof(enable_path), pin, PWM_ENABLE_FILENAME);
    // If the PWM sysfs already exists, consider it exported.
    if (access(enable_path, F_OK) == 0) return true;

    // Try to export the PWM using helper tool.
    char cmd[128];
    int n = snprintf(cmd, sizeof(cmd), "beagle-pwm-export --pin %s", pin);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        fprintf(stderr, "PWM_export_pin: command buffer overflow\n");
        return false;
    }
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "PWM_export_pin: beagle-pwm-export failed (rc=%d)\n", rc);
        return false;
    }

    // Wait briefly for sysfs entries to appear
    for (int i = 0; i < 20; ++i) {
        if (access(enable_path, F_OK) == 0) return true;
        usleep(100000); // 100 ms
    }
    fprintf(stderr, "PWM_export_pin: timeout waiting for %s\n", enable_path);
    return false;
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

// Helper: write to a specific pin file
static bool writeToPinFile(const char* pin, const char* filename, const char* value)
{
    char path[PATH_MAX];
    build_path(path, sizeof(path), pin, filename);
    return writeToFile(path, value);
}

// PWM helper Functions
bool PWM_setDutyCycle(int dutyCycle){
    #ifdef DEBUG
    printf("Setting duty cycle to %d\n", dutyCycle);
    #endif
    //writeToFile(PWM_DUTY_CYCLE_FILE, "0"); // --- IGNORE ---
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d", dutyCycle);
    if (n < 0) return false;
    return PWM_setDutyCycle_pin(DEFAULT_PWM_PIN, dutyCycle);
}

bool PWM_setDutyCycle_pin(const char* pin, int dutyCycle)
{
    #ifdef DEBUG
    printf("Setting duty cycle on %s to %d\n", pin, dutyCycle);
    #endif
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d", dutyCycle);
    if (n < 0) return false;
    return writeToPinFile(pin, PWM_DUTY_FILENAME, buf);
}

bool PWM_setPeriod(int period){
    #ifdef DEBUG
    printf("Setting period to %d\n", period);
    #endif
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d", period);
    if (n < 0) return false;
    return PWM_setPeriod_pin(DEFAULT_PWM_PIN, period);
}

bool PWM_setPeriod_pin(const char* pin, int period)
{
    #ifdef DEBUG
    printf("Setting period on %s to %d\n", pin, period);
    #endif
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d", period);
    if (n < 0) return false;
    return writeToPinFile(pin, PWM_PERIOD_FILENAME, buf);
}


bool PWM_setFrequency(int Hz, int dutyCyclePercent)
{
    if (dutyCyclePercent < 0 || dutyCyclePercent > 100) {
        fprintf(stderr, "PWM_setFrequency: invalid duty cycle percentage\n");
        return false;
    }

    /* from A2
    // Assignment spec: support 0 - 500 Hz
    if (Hz < 0)   Hz = 0;
    if (Hz > 500) Hz = 500;

    if (Hz == 0) {
        // 0 Hz -> LED off / stop PWM
        return PWM_disable();
    }
    */

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
    if (!PWM_disable_pin(DEFAULT_PWM_PIN)) return false;

    if (!PWM_setPeriod_pin(DEFAULT_PWM_PIN, (int)period_ull)) return false;
    if (!PWM_setDutyCycle_pin(DEFAULT_PWM_PIN, (int)duty_ull)) return false;

    PWM_enable_pin(DEFAULT_PWM_PIN);
    return true;
}
bool PWM_setFrequency_pin(const char* pin, int Hz, int dutyCyclePercent)
{
    if (dutyCyclePercent < 0 || dutyCyclePercent > 100) {
        fprintf(stderr, "PWM_setFrequency_pin: invalid duty cycle percentage\n");
        return false;
    }

    if (Hz < 0)   Hz = 0;
    if (Hz > 500) Hz = 500;

    if (Hz == 0) {
        return PWM_disable_pin(pin);
    }

    const unsigned long long NSEC = 1000000000ULL;
    unsigned long long period_ull = NSEC / (unsigned long long)Hz;
    unsigned long long duty_ull   = (period_ull * (unsigned long long)dutyCyclePercent) / 100ULL;

    if (!PWM_disable_pin(pin)) return false;
    if (!PWM_setPeriod_pin(pin, (int)period_ull)) return false;
    if (!PWM_setDutyCycle_pin(pin, (int)duty_ull)) return false;
    PWM_enable_pin(pin);
    return true;
}
bool PWM_enable(){
    #ifdef DEBUG
    printf("Enabling PWM\n");
    #endif
    return PWM_enable_pin(DEFAULT_PWM_PIN);
}

bool PWM_disable(){
    #ifdef DEBUG
    printf("Disabling PWM\n");
    #endif
    return PWM_disable_pin(DEFAULT_PWM_PIN);
}

bool PWM_enable_pin(const char* pin)
{
    return writeToPinFile(pin, PWM_ENABLE_FILENAME, "1");
}

bool PWM_disable_pin(const char* pin)
{
    return writeToPinFile(pin, PWM_ENABLE_FILENAME, "0");
}