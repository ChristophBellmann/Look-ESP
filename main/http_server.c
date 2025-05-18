#include "http_server.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "camera.h"

static const char *TAG = "http";

// HTML-Indexseite
static const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>ESP32-CAM Control</title>
  </head>
  <body>
    <h1>ESP32-CAM</h1>
    <button onclick="fetch('/snapshot').then(r=>r.blob()).then(b=>{document.getElementById('img').src=URL.createObjectURL(b)})">Snapshot</button>
    <button onclick="document.getElementById('img').src='/stream'">Live-Stream</button>
    <br><br>
    <img id="img" width="640"/>
  </body>
</html>
)rawliteral";

// Handler für Index (/)
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %d", err);
        return err;
    }
    // Register URI handlers
    httpd_uri_t uris[] = {
        { .uri = "/",         .method = HTTP_GET, .handler = index_handler },
        { .uri = "/snapshot", .method = HTTP_GET, .handler = snapshot_handler },
        { .uri = "/stream",   .method = HTTP_GET, .handler = stream_handler },
    };
    for (int i = 0; i < sizeof(uris)/sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    ESP_LOGI(TAG, "HTTP Server started");
    return ESP_OK;
}
