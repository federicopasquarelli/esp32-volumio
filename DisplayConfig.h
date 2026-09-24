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

// LvglHandler.cpp's flush hands LVGL's little-endian RGB565 buffer to draw16bitBeRGBBitmap(),
// which sends the bytes as they are in memory where the ILI9341 expects the high byte first -- so
// every colour reaches the panel with its two bytes swapped (that's why COLOR_ACCENT, 0x1DB954,
// renders blue-violet instead of green; black and white are unaffected). Deliberately left as is
// (see CLAUDE.md), but anything where the *actual* colour matters -- the light colour palette in
// TuyaLights.cpp -- pre-swaps to cancel it. If the flush is ever switched to
// draw16bitRGBBitmap() (which swaps correctly), set this to 0 or that palette will go wrong.
#define DISPLAY_BYTE_SWAPPED 1

#endif
