
## config
`alias get_idf='. $HOME/esp/esp-idf/export.sh'`

    sudo chmod a+rwx /dev/ttyUSB0
    get_idf
    idf.py set-target esp32
    idf.py menuconfig

## build

    idf.py build

## run

    idf.py -p /dev/ttyUSB0 flash && idf.py -p /dev/ttyUSB0 monitor

stop the monitor with `ctrl+alt+5`

# dependencies

* ArduinoJSON 5.13.3
* WS2812
* GeneraUtils

details in [libs/readme](../libs/readme.md)
