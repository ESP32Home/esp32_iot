# esp32_iot
ESP32 projects reference based on the official ESP-IDF taking advantage of OTA MQTT JSON and others

# documentation
https://esp32home.github.io/esp32_iot/

# Requirements
* the [official esp-idf SDK](https://github.com/espressif/esp-idf)

    cd ~/esp-idf
    git fetch
    git checkout cc8ad721f98ffbc7953ece70616c07422b58e06b

using esp-idf commit from Tue Aug 28 11:01:22

# Used module
* ESP-WROOM-32

# rgb_led
test vectors
## all
all leds with the same rgb

    mosquitto_pub -t 'esp/curvy/pixels/all' -m '{"red":0,"green":20,"blue":3}'
## one
one indexed led access

    mosquitto_pub -t 'esp/curvy/pixels/one' -m '{"index":0,"red":0,"green":20,"blue":30}'
## list
list with a color for every led in raw : [r0,g0,b0,r1,g1,b1,...]

570 bytes buffer required instead of 1080 for multiple r,g,b json objects

    mosquitto_pub -t 'esp/curvy/pixels/list' -m '{"leds":[10,2,3,0,2,30,10,2,3,0,2,30,10,2,3,0,2,30,20,3,15]}'
## grad
gradient of colors
    mosquitto_pub -t 'esp/curvy/pixels/grad' -m '{"led_start":0, "nb_leds":10, "col_start":{"r":0,"g":10,"b":0}, "col_stop":{"r":0,"g":0,"b":10}}'

    mosquitto_pub -t 'esp/curvy/pixels/grad' -m '{"led_start":0, "nb_leds":256, "col_start":{"r":0,"g":10,"b":0}, "col_stop":{"r":0,"g":0,"b":10}}'

## panel
### flash
    mosquitto_pub -t 'esp/curvy/panel' -m '{"action":"flash", "duration_ms":1000,"r":0,"g":10,"b":0}'

### wave
    mosquitto_pub -t 'esp/curvy/panel' -m '{"action":"wave", "duration_ms":1000,"length":32,"r":0,"g":10,"b":0}'

mosquitto_pub -t 'esp/curvy/panel' -m '{"action":"wave", "duration_ms":10000,"length":16,"r":0,"g":6,"b":0}'
mosquitto_pub -t 'esp/curvy/panel' -m '{"action":"wave", "duration_ms":2000,"length":32,"r":0,"g":0,"b":6}'

mosquitto_pub -t 'esp/curvy/panel' -m '{"action":"wave", "duration_ms":1000,"freq":1,"length":32,"r":0,"g":0,"b":6}'


mosquitto_pub -t 'esp/curvy/panel' -m '{"action":"wave", "duration_ms":2000,"freq":1,"length":16,"r":0,"g":8,"b":0}'
mosquitto_pub -t 'esp/curvy/panel' -m '{"action":"wave", "duration_ms":2000,"freq":-1,"length":8,"r":0,"g":0,"b":6}'

### higher waves
mosquitto_pub -t 'esp/curvy/panel' -m '{"action":"wave", "duration_ms":10000,"freq":1,"length":32,"r":0,"g":180,"b":0}'
mosquitto_pub -t 'esp/curvy/panel' -m '{"action":"wave", "duration_ms":10000,"freq":-1,"length":32,"r":0,"g":0,"b":255}'

# brightness

    mosquitto_pub -t 'esp/curvy/brightness' -m '2'

# flame

    mosquitto_pub -t 'esp/curvy/flame' -m 'burn'
    mosquitto_pub -t 'esp/curvy/flame' -m '{"r":226, "g":121, "b":35, "random":55, "period":20000, "duration_ms":5000}'

    mosquitto_pub -t 'esp/curvy/flame' -m '{"r":226, "g":121, "b":35, "random":50, "nb_leds":60, "period":15000, "duration_ms":10000}'