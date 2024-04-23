#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

static const char *TAG = "ADC EXAMPLE";
const int ITERATIONS = 5;

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float average_temp;
uint32_t temp;
void app_main(void)
{
    // maybe interesting
    // https://github.com/espressif/arduino-esp32/issues/5503
    esp_err_t ret = ESP_OK;
    uint32_t voltage = 0;
    adc1_config_width(ADC_WIDTH_BIT_12);

    while(1) {
        average_temp = 0;
        for (int i = 0; i < ITERATIONS; ++i) {
            temp = adc1_get_raw(ADC1_CHANNEL_0);
            // https://www.analog.com/en/products/tmp36.html#:~:text=The%20TMP36%20is%20specified%20from,a%20single%202.7%20V%20supply.
            average_temp += 90.0-(temp/50.0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        //average_temp /= 10;
        ESP_LOGI(TAG, "temp: %f", (average_temp/ITERATIONS)); // average_temp/ITERATIONS
    }



}