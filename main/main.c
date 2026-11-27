#include <stdint.h>
#include "display_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui.h"

#define LVGL_TASK_STACK_SIZE 8192
#define LVGL_TASK_PRIORITY   5
#define LVGL_TASK_CORE       1

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1)
    {
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    init_display();
    ui_init();
    
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL,
                            LVGL_TASK_PRIORITY, NULL, LVGL_TASK_CORE);
}