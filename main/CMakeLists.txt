idf_component_register(
    SRCS "main.c" "display_config.c"
    INCLUDE_DIRS "."
    REQUIRES
    #     ui_lvgl
        esp_driver_gpio
        esp_lcd
        freertos
)