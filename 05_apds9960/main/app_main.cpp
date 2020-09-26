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

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "WS2812.h"
#include "ArduinoJson.hpp"

static const char *TAG          = "MQTT_EXAMPLE";
static const char* TOPIC_LIST   = "esp/rgb led/list";
static const char* TOPIC_ALL    = "esp/rgb led/all";
static const char* TOPIC_ONE    = "esp/rgb led/one";
static const char* TOPIC_STATUS = "esp/rgb led/status";
static const char* TOPIC_SUB    = "esp/rgb led/#";
static const char* TOPIC_BATTERY = "esp/rgb led/battery";



static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

esp_mqtt_client_handle_t g_client;
bool is_client_ready = false;

const gpio_num_t BLUE_LED=(gpio_num_t)2;
const gpio_num_t RGB_GPIO=(gpio_num_t)13;
const gpio_num_t V_BAT_GPIO=(gpio_num_t)15;

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
static esp_adc_cal_characteristics_t *adc_chars;
static const adc1_channel_t channel = ADC1_CHANNEL_5; //GPIO33 for ADC1
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

static const uint8_t g_nb_led = 7;

WS2812 my_rgb(RGB_GPIO,g_nb_led);


static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "Characterized using eFuse Vref\n");
    } else {
        ESP_LOGI(TAG, "Characterized using Default Vref\n");
    }
}


void adc_init()
{
    ESP_LOGI(TAG, "ADC Init\n");
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel, atten);
    adc_chars = new esp_adc_cal_characteristics_t;
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);
}

uint32_t adc_read()
{
    uint32_t adc_reading = adc1_get_raw((adc1_channel_t)channel);
    return esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
}

void rgb_led_set_all(const char * payload,int len)
{
    ArduinoJson::StaticJsonBuffer<200> jsonBuffer;
    ArduinoJson::JsonObject& root = jsonBuffer.parseObject(payload);
    uint8_t red = root["red"];
    uint8_t green = root["green"];
    uint8_t blue = root["blue"];
    ESP_LOGI(TAG, "MQTT-JSON> rgb(%u , %u , %u)",red, green, blue);

    for(int i=0;i<7;i++)
    {
        my_rgb.setPixel(i,red,green,blue);
    }
    my_rgb.show();
}

void rgb_led_set_one(const char * payload,int len)
{
    ArduinoJson::StaticJsonBuffer<200> jsonBuffer;
    ArduinoJson::JsonObject& root = jsonBuffer.parseObject(payload);
    uint8_t index = root["index"];
    uint8_t red = root["red"];
    uint8_t green = root["green"];
    uint8_t blue = root["blue"];
    ESP_LOGI(TAG, "MQTT-JSON> rgb(%u , %u , %u)",red, green, blue);

    my_rgb.setPixel(index,red,green,blue);
    my_rgb.show();
}

void rgb_led_set_list(const char * payload,int len)
{
    ArduinoJson::StaticJsonBuffer<800> jsonBuffer;
    ArduinoJson::JsonObject& root = jsonBuffer.parseObject(payload);
    if (!root.success()) 
    {
        ESP_LOGE(TAG, "MQTT-JSON> Parsing error");
        return;
    }
    int size = root["leds"].size();
    int nb_leds = size / 3;
    ESP_LOGI(TAG, "MQTT-JSON> size = %u",size);
    for(int i=0;i<nb_leds;i++)
    {
        uint8_t red = root["leds"][3*i];
        uint8_t green = root["leds"][3*i+1];
        uint8_t blue = root["leds"][3*i+2];
        my_rgb.setPixel(i,red,green,blue);
        ESP_LOGI(TAG, "MQTT-JSON> rgb[%u](%u , %u , %u)",i,red, green, blue);
    }
    my_rgb.show();
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
            msg_id = esp_mqtt_client_publish(g_client, TOPIC_STATUS, "online", 0, 1, 1);
            ESP_LOGI(TAG, "MQTT> sent publish successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(g_client, TOPIC_SUB, 0);
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
            printf("MQTT> TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("MQTT> DATA=%.*s\r\n", event->data_len, event->data);
            if(strncmp(event->topic,TOPIC_ALL,event->topic_len) == 0)
            {
                rgb_led_set_all(event->data,event->data_len);
                ESP_LOGI(TAG, "MQTT> rgb all");
            }
            else if(strncmp(event->topic,TOPIC_LIST,event->topic_len) == 0)
            {
                rgb_led_set_list(event->data,event->data_len);
                ESP_LOGI(TAG, "MQTT> rgb list");
            }
            else if(strncmp(event->topic,TOPIC_ONE,event->topic_len) == 0)
            {
                rgb_led_set_one(event->data,event->data_len);
                ESP_LOGI(TAG, "MQTT> rgb one");
            }
            else
            {
                ESP_LOGI(TAG, "MQTT> unhandled topic len=%d", event->topic_len);
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
    wifi_config_t wifi_config;
    memset(&wifi_config,0,sizeof(wifi_config_t));
    strcpy((char*)wifi_config.sta.ssid,CONFIG_WIFI_SSID);
    strcpy((char*)wifi_config.sta.password,CONFIG_WIFI_PASSWORD);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s]", CONFIG_WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

char mqtt_uri[32];
char mqtt_lwt_topic[32];
char mqtt_lwt_mesg[32];

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg;
    memset(&mqtt_cfg,0,sizeof(esp_mqtt_client_config_t));
    strcpy(mqtt_uri,CONFIG_BROKER_URL);
    strcpy(mqtt_lwt_topic,TOPIC_STATUS);
    strcpy(mqtt_lwt_mesg,"offline");
    mqtt_cfg.uri = mqtt_uri;
    mqtt_cfg.event_handle = mqtt_event_handler;
    mqtt_cfg.lwt_topic = mqtt_lwt_topic;
    mqtt_cfg.lwt_msg = mqtt_lwt_mesg;
    mqtt_cfg.lwt_msg_len = 1;
    mqtt_cfg.lwt_retain = true;

    g_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(g_client);
}

void publish_battery_voltage(int v_bat_mVolt)
{
    if(is_client_ready)
    {
        char payload[10];
        sprintf(payload,"%d",v_bat_mVolt);
        esp_mqtt_client_publish(g_client, TOPIC_BATTERY, payload, 0, 1, 0);
    }
    else
    {
        ESP_LOGW(TAG, "mqtt client not ready");
    }
}

void rgb_gpio_task(void *pvParameter)
{
    static int count = 0;
    gpio_pad_select_gpio(BLUE_LED);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLUE_LED, GPIO_MODE_OUTPUT);
    while(1) {
        gpio_set_level(BLUE_LED, 1);
        vTaskDelay(10 / portTICK_PERIOD_MS);
        gpio_set_level(BLUE_LED, 0);
        vTaskDelay(990 / portTICK_PERIOD_MS);
        if((count++ %(10*60)) == 0)
        {
            uint32_t v_bat = adc_read();
            publish_battery_voltage(v_bat);
            ESP_LOGI(TAG, "v Bat measure %d mV", v_bat);
        }
    }
}

void show_pixels(uint8_t r,uint8_t g,uint8_t b)
{
    for(int i=0;i<g_nb_led;i++)
    {
        my_rgb.setPixel(i,r,g,b);    
    }
    my_rgb.show();
}


extern "C" void app_main();

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
    adc_init();

    xTaskCreate(&rgb_gpio_task, "rgb_gpio_task", 2048, NULL, 5, NULL);

    mqtt_app_start();

    show_pixels(25,4,0);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    show_pixels(4,25,2);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    show_pixels(2,4,20);
}
