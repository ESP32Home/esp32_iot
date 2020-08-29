#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <list>
#include <stdlib.h>
#include <math.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "driver/gpio.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "WS2812.h"
#include "../ArduinoJson/ArduinoJson.hpp"

#define CONFIG_BROKER_URL "mqtt://10.0.0.42"

static const char *TAG                  = "MQTT_EXAMPLE";
static const char* TOPIC_ONE            = "esp/curvy/pixels/one";
static const char* TOPIC_ALL            = "esp/curvy/pixels/all";
static const char* TOPIC_LIST           = "esp/curvy/pixels/list";
static const char* TOPIC_GRAD           = "esp/curvy/pixels/grad";
//static const char* TOPIC_LINES_GRAD     = "esp/curvy/lines/grad";
static const char* TOPIC_PANEL          = "esp/curvy/panel";
static const char* TOPIC_BRIGHTNESS     = "esp/curvy/brightness";
static const char* TOPIC_STATUS         = "esp/curvy/status";
static const char* TOPIC_FLAME         = "esp/curvy/flame";
static const char* TOPIC_SUB            = "esp/curvy/#";


esp_timer_handle_t periodic_timer;

const static int CONNECTED_BIT = BIT0;

esp_mqtt_client_handle_t g_client;
bool is_client_ready = false;
float g_brightness = 1.0;

const gpio_num_t BLUE_LED=(gpio_num_t)2;
const gpio_num_t RGB_GPIO=(gpio_num_t)18;

//static const uint8_t g_nb_led = 24;
static const uint16_t g_nb_led = 1;
static const uint16_t g_nb_lines = 1;


struct grad_t{
    uint8_t start_red   ;
    uint8_t start_green ;
    uint8_t start_blue  ;
    uint8_t stop_red    ;
    uint8_t stop_green  ;
    uint8_t stop_blue   ;
};

struct action_flash_t{
    pixel_t color;
};

struct action_wave_t{
    pixel_t color;
    int     length;
    float   freq;
    bool    is_wavelet;
};

struct action_flame_t{
    pixel_t color;
    int     length;
    float   freq;
    int     random;
    int     nb_leds;
};

enum class action_type_t { flash, wave, wavelet, flame };

class action_t{
    public:
        bool run(WS2812* leds,int delay_ms);
    public:
        std::string name;
        int     progress_ms;
        int     duration_ms;
        action_type_t a_type;
        union{
            action_flash_t flash;
            action_wave_t wave;
            action_flame_t flame;
        };
};

class animation_t{
    public:
        animation_t(WS2812* v_leds):enabled(false),leds(v_leds){}
        void run();
        void kill();
        void add_flash(action_flash_t &v_flash,int v_duration_ms);
        void add_wave(action_wave_t &v_wave,int v_duration_ms);
        void add_wavelet(action_wave_t &v_wavelet,int v_duration_ms);
        void add_flame(action_flame_t &v_flame,int v_duration_ms);
    public:
        bool enabled;
        WS2812* leds;
        std::list<action_t> actions;

};

float flash_progress_to_intensity(int progress_ms,int duration_ms)
{
    float pos = progress_ms;
    pos /= duration_ms;//div / 0 exception
    if(pos > 0.5)
    {
        pos = 1 - pos;
    }
    return 2*pos;//so that 0.5 => 1
}

void simple_fire(WS2812* leds,action_flame_t &flame)
{
    //  Flicker, based on our initial RGB values
    for(int i=0; i<g_nb_led; i++) 
    {
        int flicker = rand() % flame.random;
        int r1 = flame.color.red-flicker;
        int g1 = flame.color.green-flicker;
        int b1 = flame.color.blue-flicker;
        if(g1<0) g1=0;
        if(r1<0) r1=0;
        if(b1<0) b1=0;
        leds->setPixel(i,r1,g1, b1);
    }
}

//https://www.tweaking4all.com/hardware/arduino/adruino-led-strip-effects/#LEDStripEffectFire
void t4all_fire(WS2812* leds,action_flame_t &flame)
{
    int Cooling = 55;
    int Sparking = 120;
    //int SpeedDelay = 15;
    static uint8_t heat[g_nb_led];//max nb leds is g_nb_leds
    int cooldown;

    // Step 1.  Cool down every cell a little
    for( int i = 0; i < flame.nb_leds; i++)
    {
        cooldown = rand() % (((Cooling * 10) / flame.nb_leds) + 2);

        if(cooldown>heat[i]) 
        {
            heat[i]=0;
        }
        else
        {
            heat[i]=heat[i]-cooldown;
        }
    }

    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for( int k= flame.nb_leds - 1; k >= 2; k--)
    {
        heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
    }

    // Step 3.  Randomly ignite new 'sparks' near the bottom
    if( (rand()%255) < Sparking )
    {
        int y = (rand()%7);
        heat[y] = heat[y] + 160+(rand() % (255-160));
        //heat[y] = random(160,255);
    }

    // Step 4.  Convert heat to LED colors
    for( int j = 0; j < flame.nb_leds; j++)
    {
        uint8_t temperature = heat[j];
        int Pixel = j;
        // Scale 'heat' down from 0-255 to 0-191
        uint8_t t192 = round((temperature/255.0)*191);

        // calculate ramp up from
        uint8_t heatramp = t192 & 0x3F; // 0..63
        heatramp <<= 2; // scale up to 0..252

        // figure out which third of the spectrum we're in:
        if( t192 > 0x80) {                     // hottest
        leds->setPixel(Pixel, flame.color.red, flame.color.green, heatramp);
        } else if( t192 > 0x40 ) {             // middle
        leds->setPixel(Pixel, flame.color.red, heatramp, flame.color.blue);
        } else {                               // coolest
        leds->setPixel(Pixel, heatramp, flame.color.green, flame.color.blue);
        }
    }
}

bool action_t::run(WS2812* leds,int delay_ms)
{
    bool done = false;
    switch(a_type)
    {
        case action_type_t::flash :
            {
                float intensity = flash_progress_to_intensity(progress_ms,duration_ms);
                uint8_t r = flash.color.red * intensity;
                uint8_t g = flash.color.green * intensity;
                uint8_t b = flash.color.blue * intensity;
                leds->add_const(r, g, b);
                ESP_LOGD(TAG, "ANIMATION> Flash intensity %0.2f: color (%u,%u,%u)",intensity,r,g,b);
            }
        break;
        case action_type_t::wave :
            {
                float t = (float)progress_ms/1000;//time in seconds since start of animation
                if(wave.is_wavelet)
                {
                    leds->add_wavelet(wave.color, t, wave.freq, wave.length, g_brightness);
                }
                else
                {
                    leds->add_wave(wave.color, t, wave.freq, wave.length, g_brightness);
                }
                ESP_LOGD(TAG, "ANIMATION> wave time %0.2f",t);
            }
        break;
        case action_type_t::flame :
            {
                //simple_fire(leds,flame);
                t4all_fire(leds,flame);
            }
        break;
        default:
        break;
    }

    ESP_LOGD(TAG, "ANIMATION> progress : %d ",progress_ms);
    progress_ms+=delay_ms;
    if(progress_ms > duration_ms)
    {
        done = true;
        ESP_LOGI(TAG, "ANIMATION> %s done",name.c_str());
    }

    return done;
}

void animation_t::add_flash(action_flash_t &v_flash,int v_duration_ms)
{
    action_t flash_action;
    flash_action.a_type = action_type_t::flash;
    flash_action.progress_ms = 0;
    flash_action.duration_ms = v_duration_ms;
    flash_action.flash = v_flash;
    actions.push_back(flash_action);
    enabled = true;
}

void animation_t::add_wave(action_wave_t &v_wave,int v_duration_ms)
{
    action_t wave_action;
    wave_action.a_type = action_type_t::wave;
    wave_action.progress_ms = 0;
    wave_action.duration_ms = v_duration_ms;
    wave_action.wave = v_wave;
    actions.push_back(wave_action);
    enabled = true;
}

void animation_t::add_flame(action_flame_t &v_flame,int v_duration_ms)
{
    action_t flame_action;
    flame_action.a_type = action_type_t::flame;
    flame_action.progress_ms = 0;
    flame_action.duration_ms = v_duration_ms;
    flame_action.flame = v_flame;
    actions.push_back(flame_action);
    enabled = true;
}

void animation_t::kill()
{
    actions.clear();
    enabled = false;
}

void animation_t::run()
{
    if(!enabled)
        return;
    leds->clear();
    std::list<action_t>::iterator action = actions.begin();
    while (action != actions.end())
    {
        bool isDone = action->run(leds,20);//the period since last run is 20 ms
        if (isDone)
        {
            actions.erase(action++);
        }
        else
        {
            ++action;
        }
    }
    if(actions.empty())
    {
        enabled = false;
        leds->clear();
    }
    leds->show();
}


static void animation_timer_callback(void* arg);

WS2812 my_rgb(RGB_GPIO,g_nb_led,g_nb_lines);

animation_t animation(&my_rgb);



void timers_init()
{
    esp_timer_create_args_t oneshot_timer_args;
    esp_timer_handle_t oneshot_timer;
    ESP_ERROR_CHECK(esp_timer_create(&oneshot_timer_args, &oneshot_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(oneshot_timer, 31536000000000ull));//one year of microseconds
    ESP_LOGI(TAG, "timer> time since startup %lld ms",esp_timer_get_time()/1000);

    esp_timer_create_args_t periodic_timer_args;
    periodic_timer_args.callback = &animation_timer_callback;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 20000));

}

int64_t g_time;

void timestamp_start()
{
    g_time = esp_timer_get_time();
}

int64_t timestamp_stop()
{
    return (esp_timer_get_time() - g_time);
}

void delay_ms(int delay)
{
    vTaskDelay(delay / portTICK_PERIOD_MS);
}

static void animation_timer_callback(void* arg)
{
    animation.run();
}


void leds_set_all(uint8_t red,uint8_t green,uint8_t blue, bool show = true)
{
    //animation.kill();
    for(int i=0;i<g_nb_led;i++)
    {
        my_rgb.setPixel(i,red,green,blue);
    }
    if(show)
    {
        my_rgb.show();
    }
}

void leds_set_gradient(int led_start,int nb_leds,grad_t grad, bool show = true)
{
    animation.kill();
    for(int i=led_start;i<(led_start + nb_leds);i++)
    {
        float coeff = (i-led_start);
        coeff/=nb_leds;
        float coeffm1 = 1 - coeff;
        uint8_t red     = grad.start_red   *coeff  + grad.stop_red  *coeffm1;
        uint8_t green   = grad.start_green *coeff  + grad.stop_green*coeffm1;
        uint8_t blue    = grad.start_blue  *coeff  + grad.stop_blue *coeffm1;
        my_rgb.setPixel(i,  red, green, blue);
        //ESP_LOGI(TAG, "MQTT-JSON> (%d)(%u , %u , %u)",i,red, green, blue);
    }
    if(show)
    {
        my_rgb.show();
    }
}

void json_led_set_all(const char * payload,int len)
{
    ArduinoJson::StaticJsonBuffer<200> jsonBuffer;
    ArduinoJson::JsonObject& root = jsonBuffer.parseObject(payload);
    uint8_t red = root["red"];
    uint8_t green = root["green"];
    uint8_t blue = root["blue"];
    ESP_LOGI(TAG, "MQTT-JSON> rgb(%u , %u , %u)",red, green, blue);

    animation.kill();
    leds_set_all(red,green,blue);
}

void json_led_set_one(const char * payload,int len)
{
    ArduinoJson::StaticJsonBuffer<200> jsonBuffer;
    ArduinoJson::JsonObject& root = jsonBuffer.parseObject(payload);
    uint8_t index = root["index"];
    uint8_t red = root["red"];
    uint8_t green = root["green"];
    uint8_t blue = root["blue"];
    ESP_LOGI(TAG, "MQTT-JSON> rgb(%u , %u , %u)",red, green, blue);

    animation.kill();
    my_rgb.setPixel(index,red,green,blue);
    my_rgb.show();
}

void json_led_set_list(const char * payload,int len)
{
    ArduinoJson::StaticJsonBuffer<4096> jsonBuffer;
    ArduinoJson::JsonObject& root = jsonBuffer.parseObject(payload);
    if (!root.success()) 
    {
        ESP_LOGE(TAG, "MQTT-JSON> Parsing error");
        return;
    }
    int size = root["leds"].size();
    int nb_leds = size / 3;
    ESP_LOGI(TAG, "MQTT-JSON> size = %u",size);
    animation.kill();
    for(int i=0;i<nb_leds;i++)
    {
        uint8_t red     = root["leds"][3*i];
        uint8_t green   = root["leds"][3*i+1];
        uint8_t blue    = root["leds"][3*i+2];
        my_rgb.setPixel(i,red,green,blue);
        ESP_LOGI(TAG, "MQTT-JSON> rgb[%u](%u , %u , %u)",i,red, green, blue);
    }
    my_rgb.show();
}

void json_led_set_grad(const char * payload,int len)
{
    ArduinoJson::StaticJsonBuffer<600> jsonBuffer;
    ArduinoJson::JsonObject& root = jsonBuffer.parseObject(payload);
    if (!root.success()) 
    {
        ESP_LOGE(TAG, "MQTT-JSON> Parsing error");
        return;
    }
    int led_start = root["led_start"];
    ESP_LOGI(TAG, "MQTT-JSON> led_start = %d",led_start);
    int nb_leds = root["nb_leds"];
    ESP_LOGI(TAG, "MQTT-JSON> nb_leds = %d",nb_leds);
    grad_t grad;
    grad.start_red   = root["col_start"]["r"];
    grad.start_green = root["col_start"]["g"];
    grad.start_blue  = root["col_start"]["b"];
    grad.stop_red    = root["col_stop"]["r"];
    grad.stop_green  = root["col_stop"]["g"];
    grad.stop_blue   = root["col_stop"]["b"];

    leds_set_gradient(led_start, nb_leds, grad);
}

void led_set_brightness(const char * payload,int len)
{
    float brightness = atof(payload);
    if((brightness > 0.01) && (brightness < 100))
    {
        g_brightness = brightness;
        ESP_LOGI(TAG, "MQTT-JSON> brightness: %0.2f ", g_brightness);
    }
    else
    {
        ESP_LOGI(TAG, "MQTT-JSON> brightness out of range: %f ", brightness);
    }
}

void led_test_flame(const char * payload,int len)
{
    ArduinoJson::StaticJsonBuffer<600> jsonBuffer;
    ArduinoJson::JsonObject& root = jsonBuffer.parseObject(payload);
    if (!root.success()) 
    {
        ESP_LOGE(TAG, "MQTT-JSON> Parsing error");
        return;
    }
    animation.kill();

    action_flame_t flame;
    int duration_ms = root["duration_ms"];
    int period = root["period"];
    flame.color.red = root["r"];
    flame.color.green = root["g"];
    flame.color.blue = root["b"];
    flame.random = root["random"];
    flame.nb_leds = root["nb_leds"];
    animation.add_flame(flame,duration_ms);
    ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, period));
}

void json_led_set_panel(const char * payload,int len)
{
    ArduinoJson::StaticJsonBuffer<600> jsonBuffer;
    ArduinoJson::JsonObject& root = jsonBuffer.parseObject(payload);
    if (!root.success()) 
    {
        ESP_LOGE(TAG, "MQTT-JSON> Parsing error");
        return;
    }
    bool do_wave = false;
    bool is_wavelet = false;
    std::string action = root["action"].as<std::string>();
    if(action.compare("off") == 0)
    {
        animation.kill();
        timestamp_start();
        leds_set_all(0,0,0,false);
        int64_t t_set_all = timestamp_stop();
        timestamp_start();
        my_rgb.show();
        int64_t t_show = timestamp_stop();
        ESP_LOGI(TAG, "MQTT-JSON> Panel Off");
        ESP_LOGI(TAG, "MQTT-JSON> time to set all: %lld us ; time to show: %lld us", t_set_all, t_show);
    }
    else if(action.compare("flash") == 0)
    {
        action_flash_t flash;
        int duration_ms = root["duration_ms"];
        flash.color.red = root["r"];
        flash.color.green = root["g"];
        flash.color.blue = root["b"];
        animation.add_flash(flash,root["duration_ms"]);
        ESP_LOGI(TAG, "MQTT-JSON> Added Flash (%u,%u,%u) for %d ms",flash.color.red,flash.color.green,flash.color.blue,duration_ms);
    }
    else if(action.compare("wave") == 0)
    {
        do_wave = true;
    }
    else if(action.compare("wavelet") == 0)
    {
        do_wave = true;
        is_wavelet = true;
    }
    if(do_wave)
    {
        action_wave_t wave;
        int duration_ms = root["duration_ms"];
        wave.length = root["length"];
        wave.freq   = root["freq"];
        wave.is_wavelet = is_wavelet;
        wave.color.red = root["r"];
        wave.color.green = root["g"];
        wave.color.blue = root["b"];
        animation.add_wave(wave,duration_ms);
        ESP_LOGI(TAG, "MQTT-JSON> Added Wave (%u,%u,%u) for %d ms",wave.color.red,wave.color.green,wave.color.blue,duration_ms);
    }
}

typedef void (*t_mqtt_handler)(const char *,int);

bool match_and_call(esp_mqtt_event_handle_t &event,const char* topic,t_mqtt_handler func)
{
    if(strncmp(event->topic,topic,event->topic_len) == 0)
    {
        (*func)(event->data,event->data_len);
        return true;
    }
    else
    {
        return false;
    }
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
            msg_id = esp_mqtt_client_publish(g_client, TOPIC_STATUS, "online", 0, 1, 0);
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

            if      (match_and_call(event,TOPIC_ALL,  &json_led_set_all))     {}
            else if (match_and_call(event,TOPIC_LIST, &json_led_set_list))    {}
            else if (match_and_call(event,TOPIC_ONE,  &json_led_set_one))     {}
            else if (match_and_call(event,TOPIC_GRAD, &json_led_set_grad))    {}
            else if (match_and_call(event,TOPIC_PANEL,&json_led_set_panel))   {}
            else if (match_and_call(event,TOPIC_BRIGHTNESS,&led_set_brightness)) {}
            else if (match_and_call(event,TOPIC_FLAME,&led_test_flame)) {}
            else
            {
                ESP_LOGI(TAG, "MQTT> unhandled topic len=%d", event->topic_len);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        case MQTT_EVENT_ANY:
            ESP_LOGI(TAG, "MQTT_EVENT_ANY");
            break;
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            break;
    }
    return ESP_OK;
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

extern "C" void app_main();
void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup..");

    ESP_LOGI(TAG, "[APP] Free memory: %d uint8_ts", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_INFO);

    ESP_LOGI(TAG, "0 g 0");
    leds_set_all(0,1,0);
    delay_ms(4000);
    ESP_LOGI(TAG, "r g b ");
    leds_set_all(1,1,1);
    delay_ms(4000);
    ESP_LOGI(TAG, "0 0 0");
    leds_set_all(0,0,0);
    delay_ms(4000);
    ESP_LOGI(TAG, "0 0 0");
    leds_set_all(0,0,0);


    nvs_flash_init();
    //timers_init(); for the led animation
    
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    timestamp_start();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "[APP] esp_netif_init in %lld ms", timestamp_stop()/1000);
    ESP_ERROR_CHECK(example_connect());
    mqtt_app_start();

    ESP_LOGI(TAG, "No demo leds");
    //leds_set_all(1,0,0);
    //delay_ms(500);
    //leds_set_all(0,1,0);
    //delay_ms(500);
    //leds_set_all(0,0,1);
    //delay_ms(500);
    //leds_set_all(0,0,0);
    //ESP_LOGI(TAG, "end of demo leds");


}
