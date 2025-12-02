#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include "doorMod.h"
#include <unistd.h>
#include <errno.h>

int main (){
    if (!initializeDoorSystem ()){
        printf("System initialization failed. Exiting.\n");
        return EXIT_FAILURE;
    } else {
        printf("System initialized successfully.\n");
    }
    
    int degrees = 80;
    if (degrees < 0 || degrees > 360){
        printf ("invalid degrees\n");
    } else {
        if (!StepperMotor_Rotate(degrees)){
            printf("Motor rotation failed.\n");
        } else {
            printf("Motor rotated successfully to %d degrees.\n", degrees);
        }
    }

    return 0;
}