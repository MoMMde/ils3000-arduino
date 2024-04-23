#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_STRIP1_G 23
void app_main(void)
{
    // Configure Digital I/O for LEDs

    gpio_set_direction(LED_STRIP1_G, GPIO_MODE_OUTPUT);

    while(1)
    {

        // LED STRIP 1
        gpio_set_level(LED_STRIP1_G, 1);
        vTaskDelay(3000);
        gpio_set_level(LED_STRIP1_G, 0);
        vTaskDelay(3000);

    }


}