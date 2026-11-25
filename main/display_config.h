#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

/* ------------------------- Resolución ------------------------- */
/* El ST7796 es nativamente 320x480 (vertical). Con MADCTL = 0x28
 * queda en horizontal 480x320. Si cambiás la orientación, cambiá
 * TFT_WIDTH/TFT_HEIGHT y TFT_MADCTL juntos. */
#define TFT_WIDTH        480
#define TFT_HEIGHT       320

/* Registro MADCTL (0x36). Valores habituales para ST7796:
 *   0x48 vertical, 0x28 horizontal, 0x88 vertical invertido,
 *   0xE8 horizontal invertido.
 * Si la imagen sale espejada o con rojo/azul cambiados, ajustá acá. */
#define TFT_MADCTL       0x28

/* Algunos módulos necesitan inversión de color para verse bien. */
#define TFT_INVERT_COLORS  0

/* ------------------------- Buffer de LVGL ------------------------- */
/* 10 líneas en RGB565 (2 bytes por píxel). */
#define DRAW_BUF_LINES   10
#define DRAW_BUF_SIZE    (TFT_WIDTH * DRAW_BUF_LINES * sizeof(uint16_t))

/* ------------------------- Pines LCD (bus paralelo 8 bits) ------------------------- */
#define LCD_PIN_D0   GPIO_NUM_23
#define LCD_PIN_D1   GPIO_NUM_19
#define LCD_PIN_D2   GPIO_NUM_18
#define LCD_PIN_D3   GPIO_NUM_17
#define LCD_PIN_D4   GPIO_NUM_16
#define LCD_PIN_D5   GPIO_NUM_14
#define LCD_PIN_D6   GPIO_NUM_13
#define LCD_PIN_D7   GPIO_NUM_4

#define LCD_PIN_WR   GPIO_NUM_25
#define LCD_PIN_DC   GPIO_NUM_26   /* RS */
#define LCD_PIN_CS   GPIO_NUM_27
#define LCD_PIN_RST  GPIO_NUM_5    /* compartido con el RST del táctil */

#define LCD_PCLK_HZ  (10 * 1000 * 1000)   /* bajalo a 10 MHz si ves glitches */

/* ------------------------- API ------------------------- */

/** Inicia pantalla, táctil y LVGL (display + indev). */
void init_display(void);

/* Hooks del táctil (deshabilitados: el proyecto no usa I2C/táctil).
 * void touch_init(void);
 * bool touch_read(uint16_t *x, uint16_t *y); */