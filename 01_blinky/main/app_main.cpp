#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_event_loop.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"

#include "esp_log.h"

static const char *TAG          = "Blinky";

const gpio_num_t BLUE_LED=(gpio_num_t)1;

extern "C" void app_main();

void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup.. blinky");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    vTaskDelay(500 / portTICK_PERIOD_MS);

    gpio_pad_select_gpio(BLUE_LED);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLUE_LED, GPIO_MODE_OUTPUT);
    while(1) {
        gpio_set_level(BLUE_LED, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(BLUE_LED, 0);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

}
