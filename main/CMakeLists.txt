idf_component_register(
    SRCS
        "main.c"
        "http_server.c"
        "camera.c"
        "wifi.c"           # ← Hier hinzufügen
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_server
        esp_camera
        esp_netif
        esp_wifi
        nvs_flash 
        log
)
