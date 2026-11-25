/**
 * main.c — diagnóstico mínimo ST7796 (bus i80 de 8 bits), SIN LVGL.
 *
 * Objetivo: confirmar que la pantalla muestra algo y que no hay
 * errores de GPIO ni de task_wdt. Usa los pines y parámetros de
 * display_config.h (LCD_PIN_*, TFT_WIDTH, TFT_HEIGHT, TFT_MADCTL...).
 *
 * Nota: el i80 en ESP32 clásico SIEMPRE usa DMA internamente (I2S);
 * no se puede desactivar. Para que cada envío sea "síncrono" se usa
 * un semáforo que se libera cuando termina el DMA.
 *
 * Para compilar solo esto, en main/CMakeLists.txt dejá únicamente
 * "main.c" en SRCS (así no entra display_config.c ni LVGL).
 */

#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_i80.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "display_config.h"

static const char *TAG = "LCD_TEST";

/* El ST7796 espera el byte alto primero; en memoria (little-endian)
 * hay que intercambiar los bytes de cada color RGB565. */
#define SWAP16(c)    ((uint16_t)((((c) & 0xFF) << 8) | (((c) >> 8) & 0xFF)))
#define COLOR_RED    SWAP16(0xF800)
#define COLOR_WHITE  SWAP16(0xFFFF)

static esp_lcd_panel_io_handle_t s_io = NULL;
static SemaphoreHandle_t s_done = NULL;

/* ----------------------- Buffers pequeños en RAM interna ----------------------- */

#define FILL_LINES     10
static uint16_t s_line_buf[TFT_WIDTH * FILL_LINES];

#define GLYPH_SCALE    4
#define CELL_W         (6 * GLYPH_SCALE)   /* 5 columnas + 1 de espacio */
#define CELL_H         (7 * GLYPH_SCALE)
static uint16_t s_glyph_buf[CELL_W * CELL_H];

/* ----------------------- Init del controlador ----------------------- */

typedef struct {
    uint8_t cmd;
    uint8_t data[14];
    uint8_t len;
    uint16_t delay_ms;
} lcd_init_cmd_t;

static const lcd_init_cmd_t s_init_cmds[] = {
    {0x01, {0}, 0, 120},
    {0x11, {0}, 0, 120},
    {0xF0, {0xC3}, 1, 0},
    {0xF0, {0x96}, 1, 0},
    {0x36, {TFT_MADCTL}, 1, 0},
    {0x3A, {0x55}, 1, 0},                        /* RGB565 */
    {0xB4, {0x01}, 1, 0},
    {0xB6, {0x80, 0x02, 0x3B}, 3, 0},
    {0xE8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
    {0xC1, {0x06}, 1, 0},
    {0xC2, {0xA7}, 1, 0},
    {0xC5, {0x18}, 1, 120},
    {0xE0, {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54,
            0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B}, 14, 0},
    {0xE1, {0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B, 0x43,
            0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B}, 14, 120},
    {0xF0, {0x3C}, 1, 0},
    {0xF0, {0x69}, 1, 120},
#if TFT_INVERT_COLORS
    {0x21, {0}, 0, 0},
#endif
    {0x29, {0}, 0, 20},
};

/* Se ejecuta desde ISR cuando termina el DMA de un bloque de color. */
static bool color_done_cb(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static void lcd_init(void)
{
    ESP_LOGI(TAG, "creando bus i80 (8 bits)");
    esp_lcd_i80_bus_handle_t bus = NULL;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = LCD_PIN_DC,
        .wr_gpio_num = LCD_PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
            LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
        },
        .bus_width = 8,
        .max_transfer_bytes = sizeof(s_line_buf),
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_cfg, &bus));
    ESP_LOGI(TAG, "bus i80 ok");

    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 1,
        .on_color_trans_done = color_done_cb,
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
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(bus, &io_cfg, &s_io));
    ESP_LOGI(TAG, "panel io ok");

    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << LCD_PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "reset ok");

    for (size_t i = 0; i < sizeof(s_init_cmds) / sizeof(s_init_cmds[0]); i++) {
        const lcd_init_cmd_t *c = &s_init_cmds[i];
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, c->cmd, c->len ? c->data : NULL, c->len));
        if (c->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
        }
    }
    ESP_LOGI(TAG, "init cmds ok");
}

/* ----------------------- Dibujo básico ----------------------- */

static void lcd_set_window(int x1, int y1, int x2, int y2)
{
    uint8_t caset[4] = {x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF};
    uint8_t raset[4] = {y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF};
    esp_lcd_panel_io_tx_param(s_io, 0x2A, caset, 4);
    esp_lcd_panel_io_tx_param(s_io, 0x2B, raset, 4);
}

/* Envía píxeles y espera a que termine el DMA (envío síncrono). */
static void lcd_write_pixels(const uint16_t *buf, size_t pixel_count)
{
    esp_lcd_panel_io_tx_color(s_io, 0x2C, buf, pixel_count * sizeof(uint16_t));
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "timeout esperando fin de DMA");
    }
}

static void lcd_fill(uint16_t color)
{
    for (size_t i = 0; i < TFT_WIDTH * FILL_LINES; i++) {
        s_line_buf[i] = color;
    }
    for (int y = 0; y < TFT_HEIGHT; y += FILL_LINES) {
        int lines = (TFT_HEIGHT - y) < FILL_LINES ? (TFT_HEIGHT - y) : FILL_LINES;
        lcd_set_window(0, y, TFT_WIDTH - 1, y + lines - 1);
        lcd_write_pixels(s_line_buf, (size_t)TFT_WIDTH * lines);
    }
}

/* ----------------------- Fuente bitmap 5x7 (solo lo necesario) ----------------------- */
/* Cada carácter son 5 columnas; el bit 0 de cada byte es la fila superior. */

typedef struct {
    char c;
    uint8_t col[5];
} glyph_t;

static const glyph_t s_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'o', {0x38, 0x44, 0x44, 0x44, 0x38}},
    {'l', {0x00, 0x41, 0x7F, 0x40, 0x00}},
    {'a', {0x20, 0x54, 0x54, 0x54, 0x78}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
};

static const uint8_t *font_lookup(char c)
{
    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); i++) {
        if (s_font[i].c == c) {
            return s_font[i].col;
        }
    }
    return s_font[0].col; /* desconocido -> espacio */
}

static void draw_char(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    const uint8_t *cols = font_lookup(c);

    for (int i = 0; i < CELL_W * CELL_H; i++) {
        s_glyph_buf[i] = bg;
    }
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (cols[col] & (1 << row)) {
                for (int dy = 0; dy < GLYPH_SCALE; dy++) {
                    for (int dx = 0; dx < GLYPH_SCALE; dx++) {
                        int px = col * GLYPH_SCALE + dx;
                        int py = row * GLYPH_SCALE + dy;
                        s_glyph_buf[py * CELL_W + px] = fg;
                    }
                }
            }
        }
    }
    lcd_set_window(x, y, x + CELL_W - 1, y + CELL_H - 1);
    lcd_write_pixels(s_glyph_buf, CELL_W * CELL_H);
}

static void draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s; s++, x += CELL_W) {
        draw_char(x, y, *s, fg, bg);
    }
}

/* ----------------------- app_main ----------------------- */

void app_main(void)
{
    s_done = xSemaphoreCreateBinary();

    lcd_init();

    ESP_LOGI(TAG, "rellenando pantalla de rojo");
    lcd_fill(COLOR_RED);

    const char *msg = "Hola ESP32";
    int text_w = (int)strlen(msg) * CELL_W;
    int x = (TFT_WIDTH - text_w) / 2;
    int y = (TFT_HEIGHT - CELL_H) / 2;
    draw_text(x, y, msg, COLOR_WHITE, COLOR_RED);
    ESP_LOGI(TAG, "texto dibujado");

    /* Bucle con pausa: cede CPU a IDLE y evita el watchdog. */
        bool inv = false;
    while (1) {
        inv = !inv;
        // esp_lcd_panel_io_tx_param(s_io, inv ? 0x21 : 0x20, NULL, 0);
        // ESP_LOGI(TAG, "inversion %s", inv ? "ON" : "OFF");
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}