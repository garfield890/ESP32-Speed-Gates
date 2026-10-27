#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define MAX_APS 30

static wifi_ap_record_t recs[MAX_APS];

static const char *auth_name(wifi_auth_mode_t m)
{
    switch (m) {
        case WIFI_AUTH_OPEN:          return "open";
        case WIFI_AUTH_WEP:           return "WEP";
        case WIFI_AUTH_WPA_PSK:       return "WPA";
        case WIFI_AUTH_WPA2_PSK:      return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK:      return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default:                      return "other";
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    while (1) {
        wifi_scan_config_t scan = {0};
        scan.show_hidden = true;

        esp_err_t err = esp_wifi_scan_start(&scan, true);     // blocks until the scan finishes
        if (err != ESP_OK) {
            printf("scan failed: %s\n", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        uint16_t total = 0;
        esp_wifi_scan_get_ap_num(&total);
        uint16_t n = MAX_APS;
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&n, recs));

        printf("\n--- found %u networks (showing %u) ---\n", total, n);
        for (int i = 0; i < n; i++) {
            printf("%-32s ch %2d  rssi %4d  %-9s  bytes:",
                   (char *)recs[i].ssid, recs[i].primary, recs[i].rssi, auth_name(recs[i].authmode));
            for (size_t j = 0; j < strlen((char *)recs[i].ssid); j++) {
                printf(" %02X", recs[i].ssid[j]);
            }
            printf("\n");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}