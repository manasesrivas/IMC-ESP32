idf_component_register(SRCS "display_config.c"
                    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_lcd
        freertos
        esp_timer
        lvgl
)
