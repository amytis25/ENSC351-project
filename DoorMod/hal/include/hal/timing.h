#ifndef TIMING_H
#define TIMING_H
#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>


long long getTimeInMs(void);

void sleepForMs(long long delayInMs);

void sleepForUs(long long delayInUs);



#endif

