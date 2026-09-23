#include "LvglHandler.h"
#include "UiHandler.h"
#include "TouchHandler.h"
#include "VolumioHandler.h"
#include "VolumioLibrary.h"
#include "VolumioQueue.h"
#include "TuyaLights.h"
#include <WiFi.h>
#include "arduino_secrets.h"
void setup() {
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
    setupLVGL();
    setupTouch();
    setupUI();
    setupLibrary(addScreen("Library"));
    setupQueue(addScreen("Queue", refreshQueue));
    setupTuyaLights(addScreen("Lights", refreshTuyaLights));
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
    loopTuyaLights();
    static unsigned long last_clock = 0;
    if (millis() - last_clock > 1000) {
        updateTime();
        last_clock = millis();
    }
    // No timer here on purpose: it recomputes from real elapsed milliseconds every loop
    // iteration (cheap — it no-ops unless the displayed second actually changed), rather than
    // being gated by its own periodic tick that could end up phase-locked with another timer.
    tickPlaybackClock();
    delay(5);
}
