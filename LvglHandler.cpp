#include "LvglHandler.h"
#include "TouchHandler.h"

Arduino_DataBus *bus = new Arduino_ESP32SPI(DC, CS, SCK, MOSI, MISO);
Arduino_GFX *gfx = new Arduino_ILI9341(bus, RST, 0, true);
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[SCREEN_WIDTH * 20];

void my_disp_flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c) {
    gfx->draw16bitBeRGBBitmap(a->x1, a->y1, (uint16_t *)&c->full, a->x2-a->x1+1, a->y2-a->y1+1);
    lv_disp_flush_ready(d);
}

void my_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    int x, y;
    if (getTouch(x, y)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void setupLVGL() {
    gfx->begin();
    gfx->setRotation(1);
    gfx->fillScreen(BLACK);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_WIDTH * 20);
    static lv_disp_drv_t d;
    lv_disp_drv_init(&d);
    d.hor_res = SCREEN_WIDTH;
    d.ver_res = SCREEN_HEIGHT;
    d.flush_cb = my_disp_flush;
    d.draw_buf = &draw_buf;
    lv_disp_drv_register(&d);

    static lv_indev_drv_t i;
    lv_indev_drv_init(&i);
    i.type = LV_INDEV_TYPE_POINTER;
    i.read_cb = my_touch_read;
    lv_indev_drv_register(&i);
}

void loopLVGL() {
    lv_timer_handler();
    lv_tick_inc(5);
}
