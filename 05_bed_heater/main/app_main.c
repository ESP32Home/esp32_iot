#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_EXAMPLE";

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

esp_mqtt_client_handle_t g_client;
bool is_client_ready = false;

#define BLUE_LED 2
#define HEATER_GPIO 15

int heat_request = 0;
int heat_timer = 0;

int atoi_n(const char * str,int len)
{
    char str_end_0[11];
    if(len > 10)
    {
        len = 10;
    }
    memcpy(str_end_0,str,len);
    str_end_0[len] = 0;
    return atoi(str_end_0);
}

int set_heat_1h(const char * payload,int len)
{
    heat_request = atoi_n(payload,len);
    heat_timer = 6*60;// 1h
    return heat_request;
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    g_client = event->client;//update g_client
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            is_client_ready = true;
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_publish(g_client, "esp/bed heater/status", "online", 0, 1, 1);
            ESP_LOGI(TAG, "MQTT> sent publish successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(g_client, "esp/bed heater/1h", 0);
            ESP_LOGI(TAG, "MQTT> sent subscribe successful, msg_id=%d", msg_id);

            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            is_client_ready = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            if(strncmp(event->topic,"esp/bed heater/1h",event->topic_len) == 0)
            {
                int heat = set_heat_1h(event->data,event->data_len);
                ESP_LOGI(TAG, "MQTT> heat request 1h at %d", heat);
            }
            else
            {
                ESP_LOGI(TAG, "MQTT> unhandled topic %s", event->topic);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);

            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s]", CONFIG_WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
        .event_handle = mqtt_event_handler,
        // .user_context = (void *)your_context
    };

    g_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(g_client);
}

void publish_heat_status(int heat,int timer)
{
    if(is_client_ready)
    {
        char payload[10];
        sprintf(payload,"%d",heat);
        int msg_id = esp_mqtt_client_publish(g_client, "esp/bed heater/heating", payload, 0, 1, 0);
        ESP_LOGD(TAG, "heater> sent publish successful, msg_id=%d", msg_id);
        sprintf(payload,"%d",timer);
        msg_id = esp_mqtt_client_publish(g_client, "esp/bed heater/timer", payload, 0, 1, 0);
        ESP_LOGD(TAG, "heater> sent publish successful, msg_id=%d", msg_id);
    }
    else
    {
        ESP_LOGW(TAG, "mqtt client not ready");
    }
}

void heater_gpio_task(void *pvParameter)
{
    gpio_pad_select_gpio(BLUE_LED);
    gpio_pad_select_gpio(HEATER_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(HEATER_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLUE_LED, GPIO_MODE_OUTPUT);
    static int loop_i=0;
    while(1) {
        ESP_LOGI(TAG, "heater> loop %d", loop_i++);
        if( (heat_request > 0) && (heat_timer > 0) )
        {
            int heat_value = heat_request;
            if(heat_value > 10)
            {
                heat_value = 10;
            }
            publish_heat_status(heat_value,heat_timer);
            int rest_value = 10 - heat_value;
            gpio_set_level(HEATER_GPIO, 1);
            gpio_set_level(BLUE_LED, 1);
            ESP_LOGI(TAG, "heater> Up on");
            vTaskDelay(heat_value * 1000 / portTICK_PERIOD_MS);
            gpio_set_level(HEATER_GPIO, 0);
            gpio_set_level(BLUE_LED, 0);
            ESP_LOGI(TAG, "heater> Down off");
            vTaskDelay(rest_value * 1000 / portTICK_PERIOD_MS);

            heat_timer--;
        }
        else
        {
            gpio_set_level(HEATER_GPIO, 0);
            gpio_set_level(BLUE_LED, 0);
            ESP_LOGI(TAG, "heater> IDLE Down off");
            static int idle_loop_i = 0;
            if((idle_loop_i++ % 20) ==0)
            {
                publish_heat_status(0,0);
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        
    }
}


void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    nvs_flash_init();
    wifi_init();

    xTaskCreate(&heater_gpio_task, "heater_gpio_task", 2048, NULL, 5, NULL);

    mqtt_app_start();
}
