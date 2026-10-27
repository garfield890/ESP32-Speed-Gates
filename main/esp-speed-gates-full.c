#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_netif.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "protocol_examples_common.h"

#define TAG "full-speed-gates"

#define ROLE_SENDER_1 0
#define ROLE_RECEIVER_2 1
#define ROLE ROLE_SENDER_1

#define ESP_BOARD_1_MAC { 0x24, 0xDC, 0xC3, 0x4A, 0x4D, 0x1C }
#define ESP_BOARD_2_MAC { 0x24, 0xDC, 0xC3, 0x49, 0xDE, 0xE4 }
#define PEER_MAC ESP_BOARD_2_MAC

#define TRIG GPIO_NUM_5
#define ECHO GPIO_NUM_18

#define SOUND_SPEED 0.0343

typedef struct {
    int64_t time;
} message_t;

static esp_now_peer_info_t peerInfo;
static uint8_t peer_mac[ESP_NOW_ETH_ALEN] = PEER_MAC;

message_t timingData = {0};

static void on_sent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) printf("send failed\n");
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    #if ROLE == ROLE_SENDER_1
        message_t board2;
        memcpy(&board2, data, sizeof(board2));
        printf("board1: %lld, board2: %lld", timingData.time, board2.time);

        if (timingData.time != 0) { 
            int64_t elapsedTime = board2.time - timingData.time;
            printf("elapsed time: %f\n", elapsedTime / 1000000.0);
            timingData.time = 0;  
        } else {
            printf("board 1 has not registered a time yet.\n");
        }
    #endif
}

void wifi_init(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());  

    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    ESP_LOGI(TAG, "--- Access Point Information ---");
    ESP_LOG_BUFFER_HEX("MAC Address", ap_info.bssid, sizeof(ap_info.bssid));
    ESP_LOG_BUFFER_CHAR("SSID", ap_info.ssid, sizeof(ap_info.ssid));
    ESP_LOGI(TAG, "Primary Channel: %d", ap_info.primary);
    ESP_LOGI(TAG, "RSSI: %d", ap_info.rssi);
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

    gpio_reset_pin(ECHO);
    gpio_set_direction(ECHO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ECHO, GPIO_PULLDOWN_ONLY);
}

void app_main(void)
{
    wifi_init();
    setup();

    static bool in_range = false;
    
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
                break;
            }
        }

        if (failed) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

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

        if (failed) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int64_t duration = end - start;
        float distance = duration * SOUND_SPEED / 2;
        printf("distance = %f\n", distance);
        
        // bool now_in = distance < 20.0f;

        // if (now_in && !in_range) {
        //     timingData.time = esp_wifi_get_tsf_time(WIFI_IF_STA);
        //     printf("%lld\n", timingData.time);
        //     if (timingData.time == 0) continue;
        //     #if ROLE == ROLE_RECEIVER_2
        //         esp_now_send(peer_mac, (uint8_t *)&timingData, sizeof(timingData));
        //     #endif
        //     printf("distance = %f\n", distance);
        // }
        // in_range = now_in;

        timingData.time = esp_wifi_get_tsf_time(WIFI_IF_STA);
        printf("Current TSF time: %lld\n", timingData.time);
        #if ROLE == ROLE_RECEIVER_2
            if (distance < 20.0) {
                esp_now_send(peer_mac, (uint8_t *)&timingData, sizeof(timingData));
            }
        #endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }   
}