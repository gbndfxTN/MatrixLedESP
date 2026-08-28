#pragma once

// HUB75 128x64
#define HUB75_R1       4
#define HUB75_G1       5
#define HUB75_B1       6
#define HUB75_R2       7
#define HUB75_G2       15
#define HUB75_B2       16
#define HUB75_A        17
#define HUB75_B        18
#define HUB75_C        8
#define HUB75_D        9
#define HUB75_E        10
#define HUB75_CLK      11
#define HUB75_LAT      12
#define HUB75_OE       13

// Display
#define MATRIX_WIDTH                  128
#define MATRIX_HEIGHT                 64
#define DISPLAY_BRIGHTNESS            50
#define DISPLAY_FPS                   30
#define DISPLAY_GIF_FPS               10
#define DISPLAY_GIF_LOOPS             3
#define DISPLAY_INACTIVITY_TIMEOUT_MS 10000
#define DISPLAY_FRAMEBUF_PUBLISH_MS   1000

// WiFi
#define WIFI_CONNECT_TIMEOUT_MS       20000
#define NTP_TIMEOUT_MS                10000
