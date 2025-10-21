#ifndef HC_SR04_H
#define HC_SR04_H
#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>

#define TRIG_GPIOCHIP 1  /* GPIO chip number for trigger pin */
#define TRIG_GPIO_LINE 33 /* GPIO27 - from gpioinfo: gpiochip1 33 "GPIO27" */
#define ECHO_GPIOCHIP 2  /* GPIO chip number for echo pin */
#define ECHO_GPIO_LINE 8 /* GPIO17 - from gpioinfo: gpiochip2 8 "GPIO17" */


bool init_hc_sr04();

long long get_distance();


#endif // HC_SR04_H