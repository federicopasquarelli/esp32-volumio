#include "LvglHandler.h"
#include "UiHandler.h"
#include "TouchHandler.h"
#include "VolumioHandler.h"
#include "VolumioLibrary.h"
#include "VolumioQueue.h"
#include <WiFi.h>
#include "arduino_secrets.h"
void setup() {
    Serial.begin(115200);
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
    setupLVGL();
    setupTouch();
    setupUI();
    addTab("Volumio");
    setupLibrary(addTab("Library"));
    setupQueue(addTab("Queue"));
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    while (WiFi.status() != WL_CONNECTED) delay(100);
    configTzTime(SECRET_TIMEZONE, "pool.ntp.org");
    setupVolumio();
    openLibraryRoot();
    refreshQueue();
}
void loop() {
    loopLVGL();
    loopVolumio();
    loopLibrary();
    loopQueue();
    static unsigned long last = 0;
    if (millis() - last > 1000) {
        updateTime();
        last = millis();
    }
    delay(5);
}
