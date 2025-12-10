#include <stdio.h>
#include <wifi.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>


void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    wifi_init();

}