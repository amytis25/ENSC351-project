// PWM.h
// ENSC 351 Fall 2025
// PWM control interface for GPIO15

#ifndef PWM_H
#define PWM_H
#include <stdbool.h>

// Sysfs layout format and defaults
#define PWM_SYSFS_PATH_FORMAT "/dev/hat/pwm/%s/%s"
#define PWM_DUTY_FILENAME "duty_cycle"
#define PWM_PERIOD_FILENAME "period"
#define PWM_ENABLE_FILENAME "enable"
#define DEFAULT_PWM_PIN "GPIO15"

// Pin-parameterized PWM helper Functions (preferred)
bool PWM_export_pin(const char* pin); // Initialization - exports PWM pin on BY-AI
// Set PWM as requested for a specific pin
bool PWM_setDutyCycle_pin(const char* pin, int dutyCycle);
bool PWM_setPeriod_pin(const char* pin, int period);
bool PWM_setFrequency_pin(const char* pin, int Hz, int dutyCyclePercent);
// Enable/disable PWM for a specific pin
bool PWM_enable_pin(const char* pin);
bool PWM_disable_pin(const char* pin);

// Backwards-compatible wrappers (operate on `GPIO15`)
bool PWM_export(void);
bool PWM_setDutyCycle(int dutyCycle);
bool PWM_setPeriod(int period);
bool PWM_setFrequency(int Hz, int dutyCyclePercent);
bool PWM_enable(void);
bool PWM_disable(void);

#endif