#include "hal/led.h"
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("LED test starting...\n");

    if (!LED_init()) {
        fprintf(stderr, "LED_init failed\n");
    }

    printf("Green 30%% steady for 2s\n");
    LED_set_green_steady(30);
    sleep(2);

    printf("Green off, red 50%% steady for 2s\n");
    LED_set_green_steady(0);       // just call PWM_setFrequency with 0 or disable
    LED_set_red_steady(50);
    sleep(2);

    printf("Red blink 5x @ 5Hz\n");
    LED_blink_red_n(5, 5, 80);

    printf("LED test done, shutting down\n");
    LED_shutdown();
    return 0;
}