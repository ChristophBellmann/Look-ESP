// camera_pins.h — für OV5640 ESP32-CAM Kit 1.2.1 (AliExpress)

#pragma once

// Power-down line not wired on this board
#define PWDN_GPIO_NUM    -1  

// Hardware Reset
#define RESET_GPIO_NUM    5  // CAM_RESET → GPIO05

// External clock
#define XCLK_GPIO_NUM    15  // CAM_XCLK   → GPIO15

// SCCB (I2C)
#define SIOD_GPIO_NUM    22  // CAM_SIOD   → GPIO22
#define SIOC_GPIO_NUM    23  // CAM_SIOC   → GPIO23

// Data bits D0–D7
#define Y2_GPIO_NUM       2  // CAM_DATA1  → GPIO02
#define Y3_GPIO_NUM      14  // CAM_DATA2  → GPIO14
#define Y4_GPIO_NUM      35  // CAM_DATA3  → GPIO35
#define Y5_GPIO_NUM      12  // CAM_DATA4  → GPIO12
#define Y6_GPIO_NUM      27  // CAM_DATA5  → GPIO27
#define Y7_GPIO_NUM      33  // CAM_DATA6  → GPIO33
#define Y8_GPIO_NUM      34  // CAM_DATA7  → GPIO34
#define Y9_GPIO_NUM      39  // CAM_DATA8  → GPIO39

// Sync signals
#define VSYNC_GPIO_NUM   18  // CAM_VSYNC  → GPIO18
#define HREF_GPIO_NUM    36  // CAM_HREF   → GPIO36
#define PCLK_GPIO_NUM    26  // CAM_PCLK   → GPIO26
