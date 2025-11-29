#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>
#include "hal/HC-SR04.h"
#include "hal/GPIO.h"
#include "hal/timing.h"
#include "hal/StepperMotor.h"
#include "doorMod.h"


// Initialize the door system
bool initializeDoorSystem (){
    if (!init_hc_sr04 () || !StepperMotor_Init () ) {
            printf("Failed to initialize door module.\n");
            return false;
        }
    return true;
}
long long  avgDistanceSample (){
    long long totalDistance = 0;
    int sample_count = 0;
    long long start_sampling_time = getTimeInMs();
    while ((getTimeInMs() - start_sampling_time) < 100){
        long long distance = get_distance();
        if (distance != -1){
            totalDistance += distance;
            sample_count++;
        }
        sleepForMs(5); // Small delay between samples
    }
    if (sample_count == 0){
        return -1; // Indicate error if no valid samples
    }

    return totalDistance / sample_count;
}


// Lock the door
Door_t lockDoor (Door_t *door){
    long long dist = avgDistanceSample();
    if (StepperMotor_GetPosition()== 180){
        printf("Door is already locked.\n");
    } else {
        if (dist >= 10) {
            printf("Door is open, cannot lock.\n");
            door->state = OPEN;
        }
        else if (dist == -1) {
            printf("Ultrasonic sensor error, cannot lock door.\n");
            door->state = UNKNOWN;
        }
        else if (dist < 10) {
            if (StepperMotor_Rotate(180)){
                printf("Door locked successfully.\n");
                // Update door state
                door->state = LOCKED;
            }
        } else {
            printf("Failed to lock the door.\n");
        }
    }
    return *door;

}

// Unlock the door
Door_t unlockDoor (Door_t *door){
    if (StepperMotor_GetPosition()== 0){
        printf("Door is already unlocked.\n");
    } else {
        if (get_distance() >= 5) {
            printf("Door is open, cannot unlock.\n");
            door->state = OPEN;
        }
        else if (StepperMotor_Rotate(180)) {
            printf("Door unlocked successfully.\n");
            // Update door state
            door->state = UNLOCKED;
        } else {
            printf("Failed to unlock the door.\n");
        }
    }
    return *door;

}

// Get the current status of the door
Door_t get_door_status (Door_t *door){
    long long distance = get_distance();
    if (StepperMotor_GetPosition()== 180){
        door->state = LOCKED;
        printf("Door is LOCKED.\n");
    } 
    else if (distance < 10 && distance != -1) {
        door->state = UNLOCKED;
        printf("Door is CLOSED and UNLOCKED.\n");
    }
    else if (distance == -1) {
        printf("Error reading distance from ultrasonic sensor.\n");
        return *door;
    }

    else if (distance >= 10) {
        door->state = OPEN;
        printf("Door is OPEN.\n");
    } 
    else {        
        door->state = UNKNOWN; // Assuming any other position means closed but not locked
        printf("Door is in an UNKNOWN state.\n");
    }
    

    return *door;

}



 