/**
 * st7796_display.c
 *
 * Implementación del display ST7796 (320x480) sobre bus paralelo Intel 8080
 * de 8 bits, para ESP32 clásico, usando el driver esp_lcd_panel_io_i80
 * de ESP-IDF y el driver genérico ST7796 de LVGL 9.1.
 *
 * Asignación de pines (ver tabla en la conversación):
 *   D0-D7 : 4, 13, 14, 16, 17, 18, 19, 23
 *   WR    : 25
 *   RS/DC : 26
 *   CS    : 27
 *   RST   : 5   (strapping pin seguro, no se usa SDIO en este proyecto)
 *
 * Pines reservados para otros periféricos del proyecto (no tocar acá):
 *   Touch I2C     : SDA=21, SCL=22
 *   Ultrasónico   : TRIG=32, ECHO=33
 *   UART (consola): TX=1,  RX=3
 */

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/* Requiere LV_USE_ST7796 = 1 (en lv_conf.h o en menuconfig/Kconfig).
 * lv_st7796.h incluye a su vez lv_lcd_generic_mipi.h, que es donde
 * se define LV_LCD_FLAG_NONE. */
#include "lvgl.h"
#include "src/drivers/display/st7796/lv_st7796.h"

/* ----------------------- Definición de pines ----------------------- */

#define LCD_PIN_D0   GPIO_NUM_23
#define LCD_PIN_D1   GPIO_NUM_29
#define LCD_PIN_D2   GPIO_NUM_18
#define LCD_PIN_D3   GPIO_NUM_17
#define LCD_PIN_D4   GPIO_NUM_16
#define LCD_PIN_D5   GPIO_NUM_14
#define LCD_PIN_D6   GPIO_NUM_13
#define LCD_PIN_D7   GPIO_NUM_4

#define LCD_PIN_WR   GPIO_NUM_25
#define LCD_PIN_DC   GPIO_NUM_26   /* RS */
#define LCD_PIN_CS   GPIO_NUM_27
#define LCD_PIN_RST  GPIO_NUM_5

#define LCD_H_RES    480
#define LCD_V_RES    320

/* Handle del panel IO. Lo guardamos a nivel de módulo porque los
 * callbacks send_cmd / send_color lo necesitan para poder transmitir. */
static esp_lcd_panel_io_handle_t s_io_handle = NULL;

/* --------------------- Callbacks pedidos por LVGL -------------------- */

/* Envía un comando corto al controlador. Debe esperar a que termine
 * la transacción (transferencia bloqueante). */
void my_lcd_send_cmd(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size,
                      const uint8_t *param, size_t param_size)
{
    (void)disp;
    (void)cmd_size; /* El ST7796 siempre usa comandos de 1 byte */
    esp_lcd_panel_io_tx_param(s_io_handle, *cmd, param, param_size);
}

/* Envía un bloque grande de píxeles. Puede hacerse en segundo plano
 * (DMA); esp_lcd_panel_io_tx_color ya maneja eso internamente y
 * dispara el callback de "listo" que usa LVGL para saber cuándo
 * puede seguir dibujando. */
void my_lcd_send_color(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size,
                        uint8_t *param, size_t param_size)
{
    (void)disp;
    (void)cmd_size;
    esp_lcd_panel_io_tx_color(s_io_handle, *cmd, param, param_size);
}

/* --------------------------- Inicialización --------------------------- */

lv_display_t *st7796_display_init(void)
{
    /* 1) Bus i80 de 8 bits: acá se declaran las líneas de datos y de
     *    control que va a manejar el periférico I2S en modo paralelo. */
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
        .max_transfer_bytes = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

    /* 2) Panel IO: es la capa que arma cada transacción (toma el CS,
     *    pone DC en modo comando o dato, tira los pulsos de WR) y
     *    expone las funciones tx_param / tx_color que usamos arriba. */
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = 20 * 1000 * 1000,     /* 20 MHz de pulso de WR */
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &s_io_handle));

    /* 3) Reset físico del controlador. Como no dejamos que el panel_io
     *    maneje el pin RST automáticamente, lo pulsamos a mano una
     *    sola vez al arrancar. */
    gpio_set_direction(LCD_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* 4) Creación del display en LVGL. El driver genérico st7796.c ya
     *    sabe qué secuencia de comandos mandarle al controlador para
     *    inicializarlo; solo necesita nuestros dos callbacks para
     *    saber CÓMO transmitir físicamente cada uno. */
    lv_display_t *disp = lv_st7796_create(
        LCD_H_RES, LCD_V_RES,
        LV_LCD_FLAG_NONE,
        my_lcd_send_cmd,
        my_lcd_send_color
    );

    return disp;
}