#include "TouchHandler.h"
#include <SPI.h>

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS);

void setupTouch() {
    touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(1);
}

bool getTouch(int &x, int &y) {
    if (touch.touched()) {
        TS_Point p = touch.getPoint();
        if (p.z < 400 || p.x <= 0 || p.y <= 0 || p.x >= 8191 || p.y >= 8191) return false;
        x = map(p.x, 200, 3700, 0, SCREEN_WIDTH);
        y = map(p.y, 240, 3800, 0, SCREEN_HEIGHT);
        return true;
    }
    return false;
}
