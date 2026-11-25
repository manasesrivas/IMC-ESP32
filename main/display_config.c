#include "display_config.h"

#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_i80.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

/* Buffer de dibujo de LVGL. Es un arreglo estático en RAM interna,
 * así que es válido para DMA. */
static uint32_t draw_buf[DRAW_BUF_SIZE / 4];

static esp_lcd_panel_io_handle_t s_io = NULL;
static lv_display_t *s_disp = NULL;

/* ------------------------- Comandos ST7796 ------------------------- */

#define ST7796_CASET   0x2A
#define ST7796_RASET   0x2B
#define ST7796_RAMWR   0x2C

typedef struct {
    uint8_t cmd;
    uint8_t data[14];
    uint8_t len;       /* cantidad de bytes de parámetros */
    uint16_t delay_ms;
} lcd_init_cmd_t;

/* Secuencia de inicialización habitual del ST7796S (la misma que usan
 * librerías como TFT_eSPI). Si tu módulo muestra colores raros o la
 * imagen se ve mal, contrastala con el datasheet de tu panel. */
static const lcd_init_cmd_t s_init_cmds[] = {
    {0x01, {0}, 0, 120},                         /* Software reset */
    {0x11, {0}, 0, 120},                         /* Sleep out */
    {0xF0, {0xC3}, 1, 0},                        /* Command set enable 1 */
    {0xF0, {0x96}, 1, 0},                        /* Command set enable 2 */
    {0x36, {TFT_MADCTL}, 1, 0},                  /* Orientación / orden RGB */
    {0x3A, {0x55}, 1, 0},                        /* RGB565 (16 bits por píxel) */
    {0xB4, {0x01}, 1, 0},                        /* Inversión de columnas */
    {0xB6, {0x80, 0x02, 0x3B}, 3, 0},            /* Display function control */
    {0xE8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
    {0xC1, {0x06}, 1, 0},                        /* Power control 2 */
    {0xC2, {0xA7}, 1, 0},                        /* Power control 3 */
    {0xC5, {0x18}, 1, 120},                      /* VCOM */
    {0xE0, {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54,
            0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B}, 14, 0},   /* Gamma + */
    {0xE1, {0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B, 0x43,
            0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B}, 14, 120}, /* Gamma - */
    {0xF0, {0x3C}, 1, 0},                        /* Command set disable 1 */
    {0xF0, {0x69}, 1, 120},                      /* Command set disable 2 */
#if TFT_INVERT_COLORS
    {0x21, {0}, 0, 0},                           /* Inversion on */
#endif
    {0x29, {0}, 0, 20},                          /* Display on */
};

/* ------------------------- Bus + controlador ------------------------- */

/* Se ejecuta (desde ISR) cuando el DMA terminó de enviar el bloque de
 * píxeles: recién ahí LVGL puede reutilizar el buffer. */
static bool lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    if (s_disp) {
        lv_display_flush_ready(s_disp);
    }
    return false;
}

static void st7796_init(void)
{
    /* 1) Bus i80 de 8 bits */
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .wr_gpio_num = LCD_PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
            LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
        },
        .bus_width = 8,
        .max_transfer_bytes = DRAW_BUF_SIZE,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

    /* 2) Panel IO (maneja CS, DC y los pulsos de WR) */
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = lcd_color_trans_done_cb,
        .user_ctx = NULL,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &s_io));

    /* 3) Reset por hardware (el pin RST va también al táctil) */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << LCD_PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* 4) Secuencia de inicialización del controlador */
    for (size_t i = 0; i < sizeof(s_init_cmds) / sizeof(s_init_cmds[0]); i++) {
        const lcd_init_cmd_t *c = &s_init_cmds[i];
        esp_lcd_panel_io_tx_param(s_io, c->cmd, c->len ? c->data : NULL, c->len);
        if (c->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
        }
    }
}

/* ------------------------- LVGL: flush ------------------------- */

static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    uint32_t total_pixels = w * h;

    /* LVGL entrega RGB565 en little-endian; el ST7796 espera el byte
     * alto primero, así que hay que intercambiar los bytes. */
    lv_draw_sw_rgb565_swap(px_map, total_pixels);

    uint8_t caset[4] = {area->x1 >> 8, area->x1 & 0xFF, area->x2 >> 8, area->x2 & 0xFF};
    uint8_t raset[4] = {area->y1 >> 8, area->y1 & 0xFF, area->y2 >> 8, area->y2 & 0xFF};

    esp_lcd_panel_io_tx_param(s_io, ST7796_CASET, caset, 4);
    esp_lcd_panel_io_tx_param(s_io, ST7796_RASET, raset, 4);
    /* Envío por DMA: lv_display_flush_ready() se llama desde
     * lcd_color_trans_done_cb cuando termina. */
    esp_lcd_panel_io_tx_color(s_io, ST7796_RAMWR, px_map, total_pixels * sizeof(uint16_t));
}

/* ------------------------- LVGL: táctil (deshabilitado) -------------------------
__attribute__((weak)) void touch_init(void) {}
__attribute__((weak)) bool touch_read(uint16_t *x, uint16_t *y)
{
    (void)x;
    (void)y;
    return false;
}

static void display_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x, y;

    if (touch_read(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
*/

/* ------------------------- LVGL: tick ------------------------- */

static uint32_t ticks(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * Inicia el driver de pantalla, el táctil y la configuración
 * para que LVGL reconozca la pantalla.
 */
void init_display(void)
{
    st7796_init();
    /* touch_init(); */
    lv_init();
    lv_tick_set_cb(ticks);

    s_disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, display_flush_cb);
    lv_display_set_buffers(s_disp, draw_buf, NULL, sizeof(draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_0);

    /* Táctil deshabilitado:
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, display_touchpad_read);
    */
}