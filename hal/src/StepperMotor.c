#include "hal/StepperMotor.h"

// #define DEBUG_STEP_MOTOR

// Global position tracking stored in motor steps (0 .. STEPS_PER_REV-1).
// Storing steps avoids repeated integer-division rounding when converting
// between steps and degrees.
static int current_step_position = 0;

// Function to set a single step pattern
static bool set_motor_pins(const int* pattern) {
    if (!write_pin_value(IN1_GPIOCHIP, IN1_GPIO_LINE, pattern[0])) return false;
    if (!write_pin_value(IN2_GPIOCHIP, IN2_GPIO_LINE, pattern[1])) return false;
    if (!write_pin_value(IN3_GPIOCHIP, IN3_GPIO_LINE, pattern[2])) return false;
    if (!write_pin_value(IN4_GPIOCHIP, IN4_GPIO_LINE, pattern[3])) return false;
    return true;
}

bool StepperMotor_Init(void) {
    printf("Initializing Stepper Motor...\n");
    
    // Initialize all pins as outputs
    if (!export_pin(IN1_GPIOCHIP, IN1_GPIO_LINE, "out") ||
        !export_pin(IN2_GPIOCHIP, IN2_GPIO_LINE, "out") ||
        !export_pin(IN3_GPIOCHIP, IN3_GPIO_LINE, "out") ||
        !export_pin(IN4_GPIOCHIP, IN4_GPIO_LINE, "out")) {
        printf("Failed to initialize motor pins\n");
        return false;
    }

    // Set initial position to 0
    current_step_position = 0;
    
    // Set all pins low initially
    if (!set_motor_pins(zero_pattern)) {
        printf("Failed to set initial pin states\n");
        return false;
    }
    
    printf("Stepper Motor initialized successfully\n");
    return true;
}

bool StepperMotor_Rotate(int target_degrees) {
    if (target_degrees < 0 || target_degrees > 360) {
        printf("Invalid target degrees: %d\n", target_degrees);
        return false;
    }

    // Calculate target position in steps
    int target_step_position = (int)((target_degrees * (long)STEPS_PER_REV + 180) / 360);
    
    // Calculate steps needed to reach target by rotating forward only
    // Use modulo arithmetic to handle wraparound
    int steps_needed = (target_step_position - current_step_position + STEPS_PER_REV) % STEPS_PER_REV;
    
    int current_step = 0;
    int sequence_index = 0;

    printf("Rotating motor to %d degrees (from current %d, need %d steps)...\n", 
        target_degrees, StepperMotor_GetPosition(), steps_needed);

    // Perform rotation forward only
    while (current_step < steps_needed) {
        // Set the current step pattern
        if (!set_motor_pins(halfstep_sequence[sequence_index])) {
            printf("Failed to set motor pins at step %d\n", current_step);
            return false;
        }

        // Move to next sequence pattern
        sequence_index = (sequence_index + 1) % 8;
        current_step++;

        // Update position in steps
        current_step_position = (current_step_position + 1) % STEPS_PER_REV;

        // Small delay between steps (adjust for speed)
        sleepForMs(2);
    }

    // Hold final position — print converted degrees (not raw steps)
    {
        int deg = (int)((current_step_position * 360L) / STEPS_PER_REV);
        printf("Rotation complete. Current position: %d degrees\n", deg);
    }
    return true;
}

int StepperMotor_GetPosition(void) {
    /* Convert current step position to degrees (integer degrees). Use
     * integer math: degrees = floor(current_step_position * 360 / STEPS_PER_REV)
     */
    return (int)((current_step_position * 360L) / STEPS_PER_REV);
}

bool StepperMotor_ResetPosition(void) {
    // Reset position to 0 degrees
    if (!set_motor_pins(zero_pattern)) {
        return false;
    }
    current_step_position = 0;
    return true;
}