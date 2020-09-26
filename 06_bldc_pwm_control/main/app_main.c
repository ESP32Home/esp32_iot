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

#include "driver/mcpwm.h"
#include "soc/mcpwm_reg.h"
#include "soc/mcpwm_struct.h"

static const char *TAG = "MQTT_EXAMPLE";

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

esp_mqtt_client_handle_t g_client;
bool is_client_ready = false;

#define BLUE_LED 2
#define PWM_GPIO 13


//You can get these value from the datasheet of servo you use, in general pulse width varies between 1000 to 2000 mocrosecond
#define SERVO_MIN_PULSEWIDTH 1000 //Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH 2000 //Maximum pulse width in microsecond
#define SERVO_MAX_DEGREE 90 //Maximum angle in degree upto which servo can rotate

static void mcpwm_example_gpio_initialize()
{
    printf("initializing mcpwm servo control gpio......\n");
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, PWM_GPIO);    //Set GPIO 18 as PWM0A, to which servo is connected
}

/**
 * @brief Use this function to calcute pulse width for per degree rotation
 *
 * @param  degree_of_rotation the angle in degree to which servo has to rotate
 *
 * @return
 *     - calculated pulse width
 */
static uint32_t servo_per_degree_init(uint32_t degree_of_rotation)
{
    uint32_t cal_pulsewidth = 0;
    cal_pulsewidth = (SERVO_MIN_PULSEWIDTH + (((SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH) * (degree_of_rotation)) / (SERVO_MAX_DEGREE)));
    return cal_pulsewidth;
}


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

void mqtt_publish_motor_status(int val)
{
    if(is_client_ready)
    {
        char payload[10];
        sprintf(payload,"%d",val);
        int msg_id = esp_mqtt_client_publish(g_client, "esp/motor/status", payload, 0, 1, 0);
        ESP_LOGD(TAG, "motor> sent publish successful, msg_id=%d", msg_id);
    }
    else
    {
        ESP_LOGW(TAG, "mqtt client not ready");
    }
}

void motor_set_degrees(uint32_t angle_deg)
{
    uint32_t angle_us = servo_per_degree_init(angle_deg);
    printf("motor> pulse width: %dus\n", angle_us);
    mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, angle_us);
}

void motor_handle_payload(const char * payload,int len)
{
    uint32_t val = atoi_n(payload,len);
    motor_set_degrees(val);
    mqtt_publish_motor_status(val);
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
            msg_id = esp_mqtt_client_publish(g_client, "esp/motor/status", "online", 0, 1, 1);
            ESP_LOGI(TAG, "MQTT> sent publish successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(g_client, "esp/motor/pwm", 0);
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
            if(strncmp(event->topic,"esp/motor/pwm",event->topic_len) == 0)
            {
                motor_handle_payload(event->data,event->data_len);
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

void motor_init()
{

    //1. mcpwm gpio initialization
    mcpwm_example_gpio_initialize();
    //2. initial mcpwm configuration
    printf("Configuring Initial Parameters of mcpwm......\n");
    mcpwm_config_t pwm_config;
    pwm_config.frequency = 50;    //frequency = 50Hz, i.e. for every servo motor time period should be 20ms
    pwm_config.cmpr_a = 0;    //duty cycle of PWMxA = 0
    pwm_config.cmpr_b = 0;    //duty cycle of PWMxb = 0
    pwm_config.counter_mode = MCPWM_UP_COUNTER;
    pwm_config.duty_mode = MCPWM_DUTY_MODE_0;
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);    //Configure PWM0A & PWM0B with above settings
}
void motor_start()
{
    motor_set_degrees(0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    motor_set_degrees(90);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    motor_set_degrees(0);
}

void alive_task(void *pvParameter)
{
    uint32_t loop_i = 0;
    while(1) 
    {
        ESP_LOGI(TAG, "motor> alive %d", loop_i++);
        gpio_set_level(BLUE_LED, 1);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        gpio_set_level(BLUE_LED, 0);
        vTaskDelay(9000  / portTICK_PERIOD_MS);
    }
        
}


void app_main()
{
    motor_init();

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);


    gpio_pad_select_gpio(BLUE_LED);
    gpio_set_direction(BLUE_LED, GPIO_MODE_OUTPUT);


    nvs_flash_init();
    wifi_init();

    xTaskCreate(&alive_task, "alive_task", 4096, NULL, 5, NULL);
    
    motor_start();
    mqtt_app_start();
}
