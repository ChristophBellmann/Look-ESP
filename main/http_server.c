// main/http_server.c
#include "http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_camera.h"
#include <string.h>

static const char *HTTP_TAG = "http";

// Index-Seite
static const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>ESP32-CAM</title></head><body>
  <h1>ESP32-CAM Control</h1>
  <button id="btn_snap">Snapshot</button>
  <button id="btn_stream">Live-Stream</button><br><br>
  <img id="display" width="640"/>
  <script>
    const img = document.getElementById('display');
    document.getElementById('btn_snap').onclick = () => {
      fetch('/snapshot').then(r=>r.blob()).then(b=>{img.src=URL.createObjectURL(b)});
    };
    document.getElementById('btn_stream').onclick = () => {
      img.src = '/stream';
    };
  </script>
</body></html>
)rawliteral";

// Snapshot-URI
static esp_err_t snapshot_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

// Stream-URI
static esp_err_t stream_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) break;
        char h[64];
        int len = snprintf(h, sizeof(h),
            "--frame\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %u\r\n\r\n",
            fb->len);
        httpd_resp_send_chunk(req, h, len);
        httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        httpd_resp_send_chunk(req, "\r\n", 2);
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_FAIL;
}

// Index-Handler
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, strlen(index_html));
}

void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri = "/", .method = HTTP_GET,
        .handler = index_handler
    });
    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri = "/snapshot", .method = HTTP_GET,
        .handler = snapshot_handler
    });
    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri = "/stream", .method = HTTP_GET,
        .handler = stream_handler
    });
    ESP_LOGI(HTTP_TAG, "HTTP Server started");
}