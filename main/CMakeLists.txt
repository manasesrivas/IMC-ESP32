file(GLOB_RECURSE PROJECT_RESOURCE "*.c")

idf_component_register(SRCS ${PROJECT_RESOURCE}
                    INCLUDE_DIRS "." "ui"
                    REQUIRES nvs_flash esp_wifi
                    )