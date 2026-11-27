idf_component_register(
    
    SRCS "main.c"
    INCLUDE_DIRS "." 
    REQUIRES
        display_config
        ui_lvgl
        freertos
)