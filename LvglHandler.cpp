#include "LvglHandler.h"
#include "TouchHandler.h"
#include "UiHandler.h"
#include "arduino_secrets.h"

// Backlight turns off after this long without a touch. Optional in arduino_secrets.h.
#ifndef SECRET_SCREEN_TIMEOUT_SEC
#define SECRET_SCREEN_TIMEOUT_SEC 30
#endif
#define SCREEN_TIMEOUT_MS ((unsigned long)(SECRET_SCREEN_TIMEOUT_SEC) * 1000UL)

Arduino_DataBus *bus = new Arduino_ESP32SPI(DC, CS, SCK, MOSI, MISO);
Arduino_GFX *gfx = new Arduino_ILI9341(bus, RST, 0, true);
// Heap, not static: the ESP32's static DRAM segment (dram0_0_seg) is already nearly full, and a
// 12.8KB static array overflows it at link time.
static uint8_t* buf = NULL;
#define BUF_SIZE (SCREEN_WIDTH * 20 * sizeof(lv_color16_t))

void my_disp_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px_map) {
    // LVGL 9's RGB565 buffer is little-endian; draw16bitRGBBitmap() byte-swaps it into the
    // high-byte-first order the ILI9341 expects. Don't use the Be variant: colours come out wrong.
    gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)px_map, a->x2-a->x1+1, a->y2-a->y1+1);
    lv_display_flush_ready(d);
}

static uint32_t my_tick() { return millis(); }

static unsigned long last_activity = 0;
static bool screen_on = true;
// True while the finger that woke the screen is still down, so it doesn't also press a button.
static bool wake_touch = false;

void my_touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
    int x, y;
    if (getTouch(x, y)) {
        last_activity = millis();
        if (!screen_on) {
            screen_on = true;
            wake_touch = true;
            digitalWrite(TFT_BL, HIGH);
        }
        if (wake_touch) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        wake_touch = false;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void setupLVGL() {
    gfx->begin();
    gfx->setRotation(1);
    gfx->fillScreen(BLACK);

    lv_init();
    lv_tick_set_cb(my_tick);

    buf = (uint8_t*)malloc(BUF_SIZE);

    lv_display_t *d = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(d, my_disp_flush);
    lv_display_set_buffers(d, buf, NULL, BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *i = lv_indev_create();
    lv_indev_set_type(i, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(i, my_touch_read);
}

void loopLVGL() {
    lv_timer_handler();

    static bool started = false;
    if (!started) {
        // Start counting from the first loop, WiFi setup can take a while.
        last_activity = millis();
        started = true;
    }
    if (screen_on && millis() - last_activity > SCREEN_TIMEOUT_MS) {
        screen_on = false;
        digitalWrite(TFT_BL, LOW);
        // Switched now, not on wake, so the player is already drawn when the backlight comes back.
        showPlayer();
    }
}
