#include <Arduino.h>
#include <WiFi.h>
#include <HX711.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <lvgl.h>
#include <ui.h>




#define TFT_HOR_RES   320
#define TFT_VER_RES   480 
#define TFT_ROTATION  LV_DISPLAY_ROTATION_180

// uint16_t calData[5] = {353, 3263, 369, 3493, 3};
uint16_t calData[5] = {372, 3214, 384, 3466, 4};
TFT_eSPI tft = TFT_eSPI();

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];



uint16_t touchX, touchY;

void my_touchpad_read( lv_indev_t * indev, lv_indev_data_t * data )
{
    bool touched = tft.getTouch( &touchX, &touchY, 600 );

    if(!touched) {
        data->state = LV_INDEV_STATE_RELEASED;
    } else {
        data->state = LV_INDEV_STATE_PRESSED;

        data->point.x = touchX;
        data->point.y = touchY;

        Serial.println("data x: "+String(touchX)+"   data y: "+String(touchY));
        
    }
}

static uint32_t my_tick(void){
    return millis();
}
byte pinData = 27;
byte pinClk = 26;
float factor_calibracion = 18580.0;
HX711 bascula;


void setup() {
    Serial.begin(115200);

    bascula.begin(pinData, pinClk);
    bascula.set_scale(factor_calibracion);
    bascula.tare();

    long zero_factor = bascula.read_average();

    Serial.print("zero factor");
    Serial.println(zero_factor);
    
    
    tft.begin();
    tft.setTouch(calData);
    
    

    lv_init();
    lv_tick_set_cb(my_tick);

    lv_display_t * disp;
    disp = lv_tft_espi_create(TFT_HOR_RES, TFT_VER_RES, draw_buf, sizeof(draw_buf));
    lv_display_set_rotation(disp, TFT_ROTATION);

    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /*Touchpad should have POINTER type*/
    lv_indev_set_read_cb(indev, my_touchpad_read);

    ui_init();
}

void loop()
{
    lv_timer_handler(); 
}
