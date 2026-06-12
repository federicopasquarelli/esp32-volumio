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

// Standard 16-bit Colors (RGB565)
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define ORANGE  0xFD20

#endif
