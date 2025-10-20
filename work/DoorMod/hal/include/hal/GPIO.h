#ifndef GPIO_H
#define GPIO_H
#include <stdio.h> // fopen, fprintf, fclose, perror
#include <stdlib.h>  // exit, EXIT_FAILURE, EXIT_SUCCESS
#include <stdbool.h>
#include <time.h>
#include <gpiod.h>



bool export_pin(int pin);
bool set_pin_direction(int pin, const char* direction);
bool write_pin_value(int pin, int value);
/* Read pin value. Returns 0 or 1 on success, or -1 on error. */
int read_pin_value(int pin);


#endif // GPIO_H