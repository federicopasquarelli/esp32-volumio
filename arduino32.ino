#include "LvglHandler.h"
#include "UiHandler.h"
#include "TouchHandler.h"
#include "VolumioHandler.h"
#include <WiFi.h>
#include "arduino_secrets.h"
void setup() {
    Serial.begin(115200);
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
    setupLVGL();
    setupTouch();
    setupUI();
    addTab("Volumio");
    lv_obj_t *t2 = addTab("Settings");
    lv_label_set_text(lv_label_create(t2), "Settings Page");
    lv_obj_set_style_text_color(lv_obj_get_child(t2, 0), lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lv_obj_get_child(t2, 0));
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    while (WiFi.status() != WL_CONNECTED) delay(100);
    configTzTime(SECRET_TIMEZONE, "pool.ntp.org");
    setupVolumio();
}
void loop() {
    loopLVGL();
    loopVolumio();
    static unsigned long last = 0;
    if (millis() - last > 1000) {
        updateTime();
        last = millis();
    }
    delay(5);
}
