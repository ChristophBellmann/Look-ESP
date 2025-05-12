// main.c — Gesamtdatei mit 5 MP, Autofokus, Webserver & Wi-Fi

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"         // vTaskDelay, pdMS_TO_TICKS
#include "driver/i2c.h"            // I2C_NUM_1
#include "driver/ledc.h"           // LEDC für XCLK
#include "driver/gpio.h"           // gpio_set_direction, usw.
#include "esp_log.h"               // ESP_LOGI, ESP_LOGE
#include "esp_err.h"               // esp_err_t
#include "esp_camera.h"            // esp_camera_init, sensor API
#include "esp_http_server.h"       // httpd_req_t, Webserver-API
#include "camera_pins.h"           // Deine Pin-Defines
#include "wifi.h"                  // wifi_init_sta()
#include "esp_wifi.h"    // für esp_wifi_sta_get_ap_info()
#include "http_server.h"           // start_webserver()

static const char *TAG = "app";

static void ip_logger_task(void *arg)
{
    esp_netif_ip_info_t info;
    wifi_ap_record_t ap;
    while (1) {
        if (wifi_get_ip_info(&info)) {
            ESP_LOGI(TAG, "Current IP: " IPSTR, IP2STR(&info.ip));
        } else {
            // noch nicht verbunden? Status & Scan anstoßen
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                ESP_LOGI(TAG, "Connected to SSID:%s, RSSI:%d", ap.ssid, ap.rssi);
            } else {
                ESP_LOGW(TAG, "Not connected, scanning APs...");
                wifi_start_scan();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// 1) XCLK auf 20 MHz (1-Bit Auflösung)
static void init_camera_xclk(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_HIGH_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .freq_hz         = 10000000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {
        .gpio_num   = XCLK_GPIO_NUM,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 1,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    ESP_LOGI(TAG, "XCLK 20 MHz auf GPIO%d aktiviert", XCLK_GPIO_NUM);
}

// 2) Reset-Pin (RESET_GPIO_NUM) hochziehen, PWDN optional umgehen
static void reset_camera(void)
{
    if (RESET_GPIO_NUM >= 0 && GPIO_IS_VALID_GPIO(RESET_GPIO_NUM)) {
        gpio_reset_pin(RESET_GPIO_NUM);
        gpio_set_direction(RESET_GPIO_NUM, GPIO_MODE_OUTPUT);
        gpio_set_level(RESET_GPIO_NUM, 1);
        ESP_LOGI(TAG, "Reset-Pin (GPIO%d) HIGH, Sensor aus Reset", RESET_GPIO_NUM);
    } else {
        ESP_LOGW(TAG, "No valid RESET pin configured, skipping reset");
    }
}

// 3) OV5640 Autofokus per Sensor-API
static void ov5640_autofocus(void)
{
    sensor_t *sensor = esp_camera_sensor_get();
    // AF idle
    sensor->set_reg(sensor, 0x3022, 0xFF, 0x00);
    // AF enable + start
    sensor->set_reg(sensor, 0x3022, 0xFF, 0x20);
    sensor->set_reg(sensor, 0x3022, 0xFF, 0x40);
    // Warten auf Fertig-Flag (Reg 0x3820, Bit0)
    for (int i = 0; i < 20; ++i) {
        int st = sensor->get_reg(sensor, 0x3820, 0xFF);
        if (st & 0x01) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    // AF beenden
    sensor->set_reg(sensor, 0x3022, 0xFF, 0x00);
}

static void init_camera(void)
{
    reset_camera();
    init_camera_xclk();
    vTaskDelay(pdMS_TO_TICKS(50));

    // interne Pull-Ups für SCCB
    gpio_set_pull_mode(SIOD_GPIO_NUM, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SIOC_GPIO_NUM, GPIO_PULLUP_ONLY);

    camera_config_t config = {
        .pin_pwdn       = PWDN_GPIO_NUM,     // meist -1 auf diesem Board
        .pin_reset      = RESET_GPIO_NUM,    // ≧0 → richtiger Reset-Pin
        .pin_xclk       = XCLK_GPIO_NUM,
        .pin_sscb_sda   = SIOD_GPIO_NUM,
        .pin_sscb_scl   = SIOC_GPIO_NUM,
        .pin_d7         = Y9_GPIO_NUM,
        .pin_d6         = Y8_GPIO_NUM,
        .pin_d5         = Y7_GPIO_NUM,
        .pin_d4         = Y6_GPIO_NUM,
        .pin_d3         = Y5_GPIO_NUM,
        .pin_d2         = Y4_GPIO_NUM,
        .pin_d1         = Y3_GPIO_NUM,
        .pin_d0         = Y2_GPIO_NUM,
        .pin_vsync      = VSYNC_GPIO_NUM,
        .pin_href       = HREF_GPIO_NUM,
        .pin_pclk       = PCLK_GPIO_NUM,
        .xclk_freq_hz   = 10000000,
        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,
        .pixel_format   = PIXFORMAT_JPEG,
        .frame_size     = FRAMESIZE_UXGA,
        .jpeg_quality   = 10,
        .fb_count       = 1,
        .grab_mode      = CAMERA_GRAB_WHEN_EMPTY,
        .sccb_i2c_port  = I2C_NUM_1,
    };
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed: 0x%x", err);
        return;
    }
    ESP_LOGI(TAG, "Camera init OK");
}

// 5) Snapshot-Handler (Autofokus + Foto)
static esp_err_t snapshot_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    // Bis zu 5 Versuche, jeweils 100 ms warten
    for (int i = 0; i < 5; i++) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "Attempt %d: No frame yet, retrying...", i+1);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        // Prüfe JPEG-Start (SOI)
        if (fb->len >= 2 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8) {
            break;  // gültiger Frame
        }
        // ungültiger Frame: freigeben und erneut holen
        ESP_LOGW(TAG, "Attempt %d: Frame without SOI, discarding...", i+1);
        esp_camera_fb_return(fb);
        fb = NULL;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed after retries");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Camera capture failed");
        return ESP_FAIL;
    }

    // Erfolgreich: JPEG zurückliefern
    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_send snapshot failed: %d", res);
    }
    return res;
}

// 6) Stream-Handler (MJPEG)
static esp_err_t stream_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) break;
        char header[64];
        int h = snprintf(header, sizeof(header),
                         "--frame\r\n"
                         "Content-Type: image/jpeg\r\n"
                         "Content-Length: %u\r\n\r\n",
                         fb->len);
        httpd_resp_send_chunk(req, header, h);
        httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        httpd_resp_send_chunk(req, "\r\n", 2);
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(100));  // ~10 FPS
    }
    return ESP_FAIL;
}

void app_main(void)
{

    ESP_LOGI(TAG, "=== ENTER app_main() ===");

    // WLAN starten
    wifi_init_sta();

    ESP_LOGI(TAG, "After wifi_init_sta()");

    xTaskCreate(
        ip_logger_task,
        "ip_logger",
        4096,
        NULL,
        5,
        NULL
    );

    // 2) Kamera initialisieren
    init_camera();

    ESP_LOGI(TAG, "After init_camera()");

    // 3) HTTP-Server aufsetzen
    if (start_webserver() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webserver");
    }
    ESP_LOGI(TAG, "After start_webserver()");

    // 4) Idle-Loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
