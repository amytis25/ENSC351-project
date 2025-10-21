#ifndef HC_SR04_H
#define HC_SR04_H
#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>

#define TRIG_GPIOCHIP 1  /* GPIO chip number for trigger pin (GPIO27 = gpio-434) */
#define TRIG_GPIO_LINE 434 /* GPIO27 */
#define ECHO_GPIOCHIP 2  /* GPIO chip number for echo pin (GPIO17 = gpio-336) */
#define ECHO_GPIO_LINE 336 /* GPIO17 */


bool init_hc_sr04();

long long get_distance();


#endif // HC_SR04_H