idf_component_register(
    SRCS
        "main.c"
        "wifi.c"
        "http_server.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_camera
        nvs_flash
        esp_wifi
        esp_http_server
        esp_event
        esp_netif
        driver
)