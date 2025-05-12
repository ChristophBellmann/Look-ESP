// main/http_server.h
#pragma once
#include "esp_err.h"

// Startet den HTTP-Server und registriert alle URIs
esp_err_t start_webserver(void);
