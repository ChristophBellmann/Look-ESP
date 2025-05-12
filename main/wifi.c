// wifi.c
#include "wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

#define MAX_AP_RECORDS 20

static const char *WIFI_TAG      = "wifi";
static esp_netif_t *s_sta_netif  = NULL;
static esp_netif_ip_info_t s_ip_info;
static bool s_ip_ready           = false;
static bool s_scan_in_progress   = false;

// statisch im BSS, nicht auf dem Stack!
static wifi_ap_record_t s_ap_records[MAX_AP_RECORDS];

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = data;
        s_ip_info  = evt->ip_info;
        s_ip_ready = true;
        ESP_LOGI(WIFI_TAG, "IP obtained: " IPSTR, IP2STR(&s_ip_info.ip));
    }
}

static void scan_done_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    // Anzahl der tatsächlich gefundenen APs
    uint16_t ap_num = MAX_AP_RECORDS;
    // Ergebnisse in den statischen Buffer holen
    esp_wifi_scan_get_ap_records(&ap_num, s_ap_records);
    ESP_LOGI(WIFI_TAG, "Scan done, %d AP(s) found:", ap_num);
    for (int i = 0; i < ap_num; i++) {
        ESP_LOGI(WIFI_TAG, "  %2d: SSID=\"%s\"  Ch=%2d  RSSI=%4d  Auth=%d",
                 i,
                 s_ap_records[i].ssid,
                 s_ap_records[i].primary,
                 s_ap_records[i].rssi,
                 s_ap_records[i].authmode);
    }
    s_scan_in_progress = false;
}

void wifi_init_sta(void)
{
    // NVS (für Wi-Fi-Config)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // TCP/IP + Event‐Loop + Netif
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    // Wi-Fi init
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Event-Handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &wifi_event_handler, NULL, NULL
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
        &scan_done_handler, NULL, NULL
    ));

    // STA-Konfig
    wifi_config_t wc = {
        .sta = {
            .ssid = "Ultranet-r3",
            .password = "Lassmichrein!",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_TAG, "wifi_init_sta finished, connecting...");
}

bool wifi_get_ip_info(esp_netif_ip_info_t *info)
{
    if (s_ip_ready && info) {
        *info = s_ip_info;
        return true;
    }
    return false;
}

void wifi_start_scan(void)
{
    if (s_scan_in_progress) return;
    wifi_scan_config_t scan_conf = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = true,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_conf, false);
    if (err == ESP_OK) {
        s_scan_in_progress = true;
        ESP_LOGI(WIFI_TAG, "AP scan started...");
    } else {
        ESP_LOGW(WIFI_TAG, "Failed to start scan: %s", esp_err_to_name(err));
    }
}
