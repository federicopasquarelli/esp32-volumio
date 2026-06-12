#ifndef LVGL_HANDLER_H
#define LVGL_HANDLER_H

#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include "DisplayConfig.h"

extern Arduino_GFX *gfx;

void setupLVGL();
void loopLVGL();

#endif
