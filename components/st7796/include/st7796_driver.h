#include "lvgl.h"

void my_lcd_send_cmd(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size,
                      const uint8_t *param, size_t param_size);
void my_lcd_send_color(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size,
                        uint8_t *param, size_t param_size);
lv_display_t *st7796_display_init(void);