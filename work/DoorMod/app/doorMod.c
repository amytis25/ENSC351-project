#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>
#include "hal/HC-SR04.h"
#include "hal/GPIO.h"
#include "hal/timing.h"
#include "hal/StepperMotor.h"

void test_ultrasonic_sensor() {
    if (init_hc_sr04()){
        printf("HC-SR04 initialized successfully.\n");
    } else {
        printf("Failed to initialize HC-SR04.\n");
        return;
    }
    while(1){
        long long distance = get_distance();
        if (distance != -1) {
            printf("Distance measured: %lld cm\n", distance);
        } else {
            printf("Failed to measure distance.\n");
        }
        sleepForMs(1000); // Wait for 1 second before next measurement
    }
}
void test_stepper_motor() {
    int position;
    StepperMotor_Init();
    printf("Rotating motor to 90 degrees...\n");
    if (StepperMotor_Rotate(90)) {
        printf("Motor rotated to 90 degrees successfully.\n");
    } else {
        printf("Failed to rotate motor to 90 degrees.\n");
    }
    position = StepperMotor_GetPosition();
    printf("Current motor position: %d degrees\n", position);
    
    printf("Resetting motor position...\n");
    if (StepperMotor_ResetPosition()) {
        printf("Motor position reset successfully.\n");
    } else {
        printf("Failed to reset motor position.\n");
    }
    position = StepperMotor_GetPosition();
    printf("Current motor position after reset: %d degrees\n", position);
}
   

int main() {
    printf("DoorMod application started.\n");
    // Application logic goes here
    //test_ultrasonic_sensor();
    test_stepper_motor();
    return 0;
}


 