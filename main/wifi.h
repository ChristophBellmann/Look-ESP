// wifi.h
#pragma once
#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>

void     wifi_init_sta(void);
bool     wifi_get_ip_info(esp_netif_ip_info_t *info);
void     wifi_start_scan(void);      // Scan anstoßen
