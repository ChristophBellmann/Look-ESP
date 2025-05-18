// main.c — Hauptanwendung: Wi-Fi, IP-Logger, Kamera (Stream+5 MP Snapshot), HTTP-Server

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"         // vTaskDelay, pdMS_TO_TICKS
#include "esp_log.h"               // ESP_LOGI, ESP_LOGW, ESP_LOGE
#include "esp_err.h"               // esp_err_t
#include "esp_netif.h"             // esp_netif_ip_info_t, IP2STR/IPSTR

#include "wifi.h"                  // wifi_init_sta(), wifi_get_ip_info(), wifi_start_scan()
#include "http_server.h"           // start_webserver()
#include "camera.h"                // camera_init(), snapshot_handler(), stream_handler()

static const char *TAG = "app";

//---------------------------------------------------------------------------
// IP-Logger: alle 10 s IP-Status oder Scan starten
//--------------------------------------------------------------------------- 
static void ip_logger_task(void *arg)
{
    esp_netif_ip_info_t info;
    while (1) {
        if (wifi_get_ip_info(&info)) {
            ESP_LOGI(TAG, "Current IP: " IPSTR, IP2STR(&info.ip));
        } else {
            ESP_LOGW(TAG, "Not connected, scanning APs...");
            wifi_start_scan();
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ENTER app_main() ===");

    // 1) WLAN starten
    wifi_init_sta();
    ESP_LOGI(TAG, "After wifi_init_sta()");

    // 2) IP-Logger starten
    xTaskCreate(ip_logger_task, "ip_logger", 4096, NULL, 5, NULL);

    // 3) Kamera initialisieren (für Stream: VGA @30 q)
    if (camera_init() != ESP_OK) {
        ESP_LOGE(TAG, "camera_init failed");
        return;
    }
    ESP_LOGI(TAG, "After camera_init()");

    // 4) HTTP-Server starten (/ , /snapshot , /stream)
    if (start_webserver() != ESP_OK) {
        ESP_LOGE(TAG, "start_webserver failed");
        return;
    }
    ESP_LOGI(TAG, "After start_webserver()");

    // 5) Idle-Loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
