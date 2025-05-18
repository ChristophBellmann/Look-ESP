#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

// Initialise camera for streaming (e.g. VGA, 30% quality)
esp_err_t camera_init(void);

// HTTP-URI-Handler für 5-MP-Snapshot
esp_err_t snapshot_handler(httpd_req_t *req);

// HTTP-URI-Handler für MJPEG-Stream
esp_err_t stream_handler(httpd_req_t *req);
