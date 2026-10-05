
#pragma once

// WiFi
const char *SSID = "xxxxxxxxxxx";
const char *WIFI_PWD = "xxxxxxxx";
const char *clientIP = "192.168.1.106";

// 2: portrait, on/off switch right side -> 0/0 top left
// 3: landscape, on/off switch at the top -> 0/0 top left
#define TFT_ROTATION 2
// all other TFT_xyz flags are defined in platformio.ini as PIO build flags

// 0: portrait, on/off switch right side -> 0/0 top left
// 1: landscape, on/off switch at the top -> 0/0 top left
#define TOUCH_ROTATION 0
#define TOUCH_SENSITIVITY 40
#define TOUCH_SDA 23
#define TOUCH_SCL 22

// Initial LCD Backlight brightness
#define TFT_LED_BRIGHTNESS 200

#define APP_NAME "ESP32 Denon AVR-1912"
#define VERSION "1.0.0"
