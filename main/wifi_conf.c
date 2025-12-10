#include "wifi.h"

void wifi_init(){

    printf("iniciando wifi\n");
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    printf("some default things\n");
    
    
    
    wifi_init_config_t wifi_conf = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&wifi_conf);
    printf("wifi driver initted\n");
    
    if(err != ESP_OK){
        printf("error al iniciar %s", esp_err_to_name(err));
        
        return;
    }
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    printf("wifi station mode set\n");
    esp_wifi_start();
    printf("wifi service started\n");
    
    esp_wifi_scan_start(NULL, true);
    printf("wifi scan started\n");
    
    uint16_t ap_number = 3;
    wifi_ap_record_t ap_records[3];
    esp_wifi_scan_get_ap_records(&ap_number, ap_records);
    
    for(int i = 0; i < 3; i++){
        wifi_ap_record_t ap = ap_records[i];
        printf("wifi name: %s\n", (const char *)ap.ssid);
        printf("mac address: %d\n", *ap.bssid);
        
        
    }
    
    esp_wifi_stop();
    printf("wifi service stopped\n");
    
    esp_wifi_deinit();
    printf("wifi driver deinitted\n");
    
}

