#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <Arduino_GFX_Library.h>

// Display SPI Pins (HSPI)
#define SCK 14
#define MOSI 13
#define MISO 12
#define CS 15
#define DC 2
#define RST -1
#define TFT_BL 21

// Touch SPI Pins (VSPI)
#define TOUCH_SCK 25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS 33

// Screen Dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Backlight turns off after this long without a touch
#define SCREEN_TIMEOUT_MS (3UL * 60UL * 1000UL)

// Accent for the selected tab and active buttons
#define COLOR_ACCENT 0x1DB954

// Height of the top bar (clock + dropdown menu toggle), above the player/library/queue screens
#define TOP_BAR_H 36

#define BLACK 0x0000

#endif
