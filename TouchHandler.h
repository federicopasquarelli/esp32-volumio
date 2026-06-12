#ifndef TOUCH_HANDLER_H
#define TOUCH_HANDLER_H

#include <XPT2046_Touchscreen.h>
#include "DisplayConfig.h"

void setupTouch();
bool getTouch(int &x, int &y);

#endif
