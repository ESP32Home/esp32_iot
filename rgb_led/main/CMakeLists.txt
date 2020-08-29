set(COMPONENT_SRCS 
    "app_main.cpp" 
    "../../libs/ws2812/WS2812.cpp" 
    "../../libs/GeneralUtils/GeneralUtils.cpp" 
    "../../libs/gpio/GPIO.cpp")
set(COMPONENT_ADD_INCLUDEDIRS   "../../libs/ArduinoJson"
                                "../../libs/ws2812"
                                "../../libs/GeneralUtils"
                                "../../libs/gpio"
                                ".")

register_component()
