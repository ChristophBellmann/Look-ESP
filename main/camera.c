#include "camera.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "camera_pins.h"
#include "driver/i2c.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_http_server.h"

static const char *TAG = "camera";

// Zwei globale Config-Strukturen
static camera_config_t stream_cfg;
static camera_config_t snap_cfg;

// XCLK (20 MHz) konfigurieren
static void init_xclk(void)
{
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_HIGH_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .freq_hz         = 20000000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num   = XCLK_GPIO_NUM,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 1,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
    ESP_LOGI(TAG, "XCLK 20 MHz auf GPIO%d", XCLK_GPIO_NUM);
}

// GPIO-Pins (Reset + SCCB-Pullups) setzen
static void init_gpio_pins(void)
{
    if (RESET_GPIO_NUM >= 0 && GPIO_IS_VALID_GPIO(RESET_GPIO_NUM)) {
        gpio_reset_pin(RESET_GPIO_NUM);
        gpio_set_direction(RESET_GPIO_NUM, GPIO_MODE_OUTPUT);
        gpio_set_level(RESET_GPIO_NUM, 1);
        ESP_LOGI(TAG, "RESET GPIO%d HIGH", RESET_GPIO_NUM);
    }
    gpio_set_pull_mode(SIOD_GPIO_NUM, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SIOC_GPIO_NUM, GPIO_PULLUP_ONLY);
}

// Basiskonfiguration kopieren und beide Modi füllen
static void prepare_configs(void)
{
    camera_config_t base = {
        .pin_pwdn       = PWDN_GPIO_NUM,
        .pin_reset      = RESET_GPIO_NUM,
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
        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,
        .xclk_freq_hz   = 20000000,
        .sccb_i2c_port  = I2C_NUM_1,
    };

    // Streaming-Konfiguration: VGA, 30% JPEG, 1 Framebuffer
    stream_cfg = base;
    stream_cfg.pixel_format = PIXFORMAT_JPEG;
    stream_cfg.frame_size   = FRAMESIZE_VGA;
    stream_cfg.jpeg_quality = 30;
    stream_cfg.fb_count     = 1;
    stream_cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

    // Snapshot-Konfiguration: 5 MP, 10% JPEG, 1 Framebuffer
    snap_cfg = base;
    snap_cfg.pixel_format = PIXFORMAT_JPEG;
    snap_cfg.frame_size   = FRAMESIZE_UXGA;
    snap_cfg.jpeg_quality = 10;
    snap_cfg.fb_count     = 1;
    snap_cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
}

// Initialisiert die Kamera im Streaming-Modus
esp_err_t camera_init(void)
{
    init_gpio_pins();
    init_xclk();
    vTaskDelay(pdMS_TO_TICKS(50));

    prepare_configs();

    esp_err_t err = esp_camera_init(&stream_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stream esp_camera_init failed: 0x%x", err);
    } else {
        ESP_LOGI(TAG, "camera_init OK (VGA @30q)");
    }
    return err;
}

// Kurzer OV5640-Autofokus
static void do_autofocus(sensor_t *s)
{
    s->set_reg(s, 0x3022, 0xFF, 0x20);
    for (int i = 0; i < 20; ++i) {
        int st = s->get_reg(s, 0x3820, 0xFF);
        if (st & 0x01) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    s->set_reg(s, 0x3022, 0xFF, 0x00);
}

// Handler für 5 MP-Snapshot mit Autofokus
esp_err_t snapshot_handler(httpd_req_t *req)
{
    // 1) Stream stoppen
    esp_camera_deinit();

    // 2) Snapshot-Modus aktivieren
    esp_err_t err = esp_camera_init(&snap_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "snap esp_camera_init failed: 0x%x", err);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "snap init fail");
        // Rückfall auf Stream
        esp_camera_deinit();
        esp_camera_init(&stream_cfg);
        return ESP_FAIL;
    }

    // 3) Autofokus (OV5640)
    sensor_t *s = esp_camera_sensor_get();
    if (s) do_autofocus(s);

    // 4) Foto aufnehmen
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "capture fail");
        esp_camera_deinit();
        esp_camera_init(&stream_cfg);
        return ESP_FAIL;
    }

    // 5) JPEG zurücksenden
    httpd_resp_set_type(req, "image/jpeg");
    err = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);

    // 6) Zurück in den Stream-Modus
    esp_camera_deinit();
    esp_camera_init(&stream_cfg);
    return err;
}

// MJPEG-Stream-Handler (VGA-Modus)
esp_err_t stream_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "stream_handler: no frame, abort");
            break;
        }
        if (fb->len >= 2 && fb->buf[0]==0xFF && fb->buf[1]==0xD8) {
            char header[64];
            int h = snprintf(header, sizeof(header),
                             "--frame\r\n"
                             "Content-Type: image/jpeg\r\n"
                             "Content-Length: %u\r\n\r\n",
                             fb->len);
            esp_err_t res = httpd_resp_send_chunk(req, header, h);
            if (res != ESP_OK) {
                ESP_LOGW(TAG, "stream_handler: header send error %d", res);
                esp_camera_fb_return(fb);
                break;
            }
            res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
            if (res != ESP_OK) {
                ESP_LOGW(TAG, "stream_handler: image send error %d", res);
                esp_camera_fb_return(fb);
                break;
            }
            res = httpd_resp_send_chunk(req, "\r\n", 2);
            if (res != ESP_OK) {
                ESP_LOGW(TAG, "stream_handler: tail send error %d", res);
                esp_camera_fb_return(fb);
                break;
            }
        }
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return ESP_OK;
}
