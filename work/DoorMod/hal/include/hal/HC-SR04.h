#ifndef HC_SR04_H
#define HC_SR04_H
#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>

#define TRIG_PIN 17 //GPIO 17 , pin 11 (SoC pin A26)
#define ECHO_PIN 27 //GPIO 27 , pin 13 (SoC pin N22)


bool init_hc_sr04();

long long get_distance();


#endif // HC_SR04_H