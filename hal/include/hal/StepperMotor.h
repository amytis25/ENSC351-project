#ifndef STEPPER_MOTOR_H
#define STEPPER_MOTOR_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "hal/GPIO.h"
#include "hal/timing.h"

// Half-step sequence (8 steps) for maximum torque
static const int halfstep_sequence[8][4] = {
    {1, 0, 0, 0},  // Step 1
    {1, 1, 0, 0},  // Step 2
    {0, 1, 0, 0},  // Step 3
    {0, 1, 1, 0},  // Step 4
    {0, 0, 1, 0},  // Step 5
    {0, 0, 1, 1},  // Step 6
    {0, 0, 0, 1},  // Step 7
    {1, 0, 0, 1}   // Step 8
};
// Full-step sequence (4 steps) for faster movement
static const int fullstep_sequence[4][4] = {
    {1, 0, 0, 1},  // Step 1: energize coils A and D
    {1, 1, 0, 0},  // Step 2: energize coils A and B
    {0, 1, 1, 0},  // Step 3: energize coils B and C
    {0, 0, 1, 1}   // Step 4: energize coils C and D
};
// zero pattern to turn off all coils
static const int zero_pattern[4] = {0, 0, 0, 0};

#define STEPS_PER_REV 4096 /* total half-steps per revolution for the stepper */
// IN1 GPIO 04, IN2 GPIO 22, IN3 GPIO 5, IN4 GPIO 6
#define IN1_GPIOCHIP 1  /* GPIO chip number for IN1 pin */
#define IN1_GPIO_LINE 38 /* GPIO38 - from gpioinfo: gpiochip1 38 "GPIO38" */
#define IN2_GPIOCHIP 1  /* GPIO chip number for IN2 pin */
#define IN2_GPIO_LINE 41 /* GPIO41 - from gpioinfo: gpiochip1 41 "GPIO41" */
#define IN3_GPIOCHIP 2  /* GPIO chip number for IN3 pin */
#define IN3_GPIO_LINE 15 /* GPIO15 - from gpioinfo: gpiochip2 15 "GPIO15" */
#define IN4_GPIOCHIP 2  /* GPIO chip number for IN4 pin */
#define IN4_GPIO_LINE 17 /* GPIO17 - from gpioinfo: gpiochip2 17 "GPIO17" */

// Function declarations
bool StepperMotor_Init(void);
bool StepperMotor_Rotate(int target_degrees);
int StepperMotor_GetPosition(void);
bool StepperMotor_ResetPosition(void);

#endif // STEPPER_MOTOR_H