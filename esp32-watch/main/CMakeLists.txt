idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_event
        esp_http_server
        esp_netif
        esp_wifi
        nvs_flash
        esp_driver_gpio
        esp_driver_i2c
        esp_timer
)
