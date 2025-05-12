// wifi.c
#include "wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include <string.h>

#define MAX_AP_RECORDS   20

static const char *WIFI_TAG            = "wifi";
static esp_netif_t *s_sta_netif        = NULL;
static esp_netif_ip_info_t s_ip_info;
static bool s_ip_ready                 = false;
static bool s_scan_in_progress         = false;
static wifi_ap_record_t s_ap_records[MAX_AP_RECORDS];

// -----------------------------------------------------------------------------
// 1) Hier kommen Deine bekannten SSID/Passwort-Kombinationen
// -----------------------------------------------------------------------------
typedef struct {
    char ssid[32];
    char password[64];
} wifi_credential_t;

static const wifi_credential_t wifi_credentials[] = {
    { "Ultranet",          "Lassmichrein!" },
    { "AndereSSID",        "AnderesPass!"  },
    // weitere Paare hier hinzufügen
};
#define WIFI_CRED_COUNT (sizeof(wifi_credentials) / sizeof(wifi_credentials[0]))

// -----------------------------------------------------------------------------
// Event-Handler: IP-Adresse erhalten
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Event-Handler: Scan abgeschlossen → APs prüfen und ggf. verbinden
// -----------------------------------------------------------------------------
static void scan_done_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    uint16_t ap_num = MAX_AP_RECORDS;
    esp_wifi_scan_get_ap_records(&ap_num, s_ap_records);
    ESP_LOGI(WIFI_TAG, "Scan done, %d AP(s) found:", ap_num);

    int found_idx = -1;
    for (int i = 0; i < ap_num; i++) {
        ESP_LOGI(WIFI_TAG, "  %2d: SSID=\"%s\"  Ch=%2d  RSSI=%4d  Auth=%d",
                 i,
                 s_ap_records[i].ssid,
                 s_ap_records[i].primary,
                 s_ap_records[i].rssi,
                 s_ap_records[i].authmode);
        // suche in den hinterlegten Credentials
        for (int j = 0; j < WIFI_CRED_COUNT; j++) {
            if (strcmp((char*)s_ap_records[i].ssid,
                       wifi_credentials[j].ssid) == 0) {
                found_idx = j;
                break;
            }
        }
        if (found_idx >= 0) {
            break;
        }
    }

    if (found_idx >= 0) {
        ESP_LOGI(WIFI_TAG, "▶ Found known SSID \"%s\", connecting…",
                 wifi_credentials[found_idx].ssid);
        wifi_config_t sta_cfg = { 0 };
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        strcpy((char*)sta_cfg.sta.ssid,     wifi_credentials[found_idx].ssid);
        strcpy((char*)sta_cfg.sta.password, wifi_credentials[found_idx].password);
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_cfg) );
        ESP_ERROR_CHECK( esp_wifi_connect() );
    } else {
        ESP_LOGW(WIFI_TAG, "✖ No known SSID in this scan");
    }

    s_scan_in_progress = false;
}

// -----------------------------------------------------------------------------
// Initialisierung der Wi-Fi-Station und Start des ersten Scans
// -----------------------------------------------------------------------------
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

    // TCP/IP + Event-Loop + Netif
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    // Wi-Fi init
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Event-Handler registrieren
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &wifi_event_handler, NULL, NULL
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
        &scan_done_handler, NULL, NULL
    ));

    // Station mode & start
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(WIFI_TAG, "wifi_init_sta finished, starting AP scan…");

    // ersten Scan anstoßen
    wifi_start_scan();
}

// -----------------------------------------------------------------------------
// Gibt bei Bedarf die letzte IP zurück
// -----------------------------------------------------------------------------
bool wifi_get_ip_info(esp_netif_ip_info_t *info)
{
    if (s_ip_ready && info) {
        *info = s_ip_info;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// AP-Scan non-blocking anstoßen
// -----------------------------------------------------------------------------
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
        ESP_LOGI(WIFI_TAG, "AP scan started…");
    } else {
        ESP_LOGW(WIFI_TAG, "Failed to start AP scan: %s",
                 esp_err_to_name(err));
    }
}
