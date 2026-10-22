#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#define ROLE_SENDER_1 1
#define ROLE_RECEIVER_2 2
#define ROLE ROLE_SENDER_1

#define ESP_BOARD_1_MAC { 0x24, 0xDC, 0xC3, 0x4A, 0x4D, 0x1C }
#define ESP_BOARD_2_MAC { 0x24, 0xDC, 0xC3, 0x49, 0xDE, 0xE4 }
#define PEER_MAC ESP_BOARD_2_MAC

#define TRIG GPIO_NUM_5
#define ECHO GPIO_NUM_18

#define SOUND_SPEED 0.0343

typedef struct {
    float distance_cm;
    char text[100];
} message_t;

static esp_now_peer_info_t peerInfo;
static uint8_t peer_mac[ESP_NOW_ETH_ALEN] = PEER_MAC;

static void on_sent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) printf("send failed\n");
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != sizeof(message_t)) return;
    message_t m;
    memcpy(&m, data, sizeof(m));
    m.text[sizeof(m.text) - 1] = '\0';

    #if ROLE == ROLE_RECEIVER_2
        printf("distance received: %.1f cm\n", m.distance_cm);
        snprintf(m.text, sizeof(m.text), "success! received by board %d", ROLE);
        esp_err_t err = esp_now_send(peer_mac, (uint8_t *)&m, sizeof(m));
        if (err != ESP_OK) printf("send error: %s\n", esp_err_to_name(err));
    #else
        printf("reply: %s\n", m.text);
    #endif
}

void wifi_init(void) {
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

void setup(void) {
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    memcpy(peerInfo.peer_addr, peer_mac, ESP_NOW_ETH_ALEN);
    peerInfo.channel = 0;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = false;

    ESP_ERROR_CHECK(esp_now_add_peer(&peerInfo));
    
    gpio_reset_pin(TRIG);
    gpio_set_direction(TRIG, GPIO_MODE_OUTPUT);
    gpio_set_level(TRIG, 0);

    gpio_reset_pin(ECHO);
    gpio_set_direction(ECHO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ECHO, GPIO_PULLDOWN_ONLY);
}

void app_main(void)
{
    wifi_init();
    setup();
    
    #if ROLE == ROLE_SENDER_1
        gpio_set_level(TRIG, 0);
        while (1) {
            esp_rom_delay_us(2);

            gpio_set_level(TRIG, 1);
            esp_rom_delay_us(10);
            gpio_set_level(TRIG, 0);

            bool failed = false;

            int64_t t0 = esp_timer_get_time();
            while (gpio_get_level(ECHO) == 0) {
                if (esp_timer_get_time() - t0 > 5000) {
                    printf("not started\n");
                    failed = true;
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    break;
                }
            }

            if (failed) continue;

            int64_t start = esp_timer_get_time();
            while (gpio_get_level(ECHO) == 1) {
                if (esp_timer_get_time() - start > 40000) { 
                    printf("stuck\n");
                    failed = true;
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    break;
                }
            }
            int64_t end = esp_timer_get_time();

            if (failed) continue;

            int64_t duration = end - start;
            float distance = duration * SOUND_SPEED / 2;
            printf("distance = %f\n", distance);

            message_t msg = {};
            msg.distance_cm = distance;

            esp_err_t err = esp_now_send(peer_mac, (uint8_t *)&msg, sizeof(msg));
            if (err != ESP_OK) printf("send error: %s\n", esp_err_to_name(err));

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    #else
        while (1) {
            int64_t now_us = esp_timer_get_time();
            printf("time = %lld us\n", (long long)now_us);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    #endif
}