idf_component_register(
    SRCS "esp-speed-gates-full.c" "esp-speed-gates.c" "esp-speed-gates.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_timer esp_driver_gpio
)