file(
    GLOB UI_SRCS
    # "images/*.c"
    "*.c"
)

idf_component_register(
    SRCS
        ${UI_SRCS}
    INCLUDE_DIRS 
        "include"
        "."
    REQUIRES
        lvgl
)
