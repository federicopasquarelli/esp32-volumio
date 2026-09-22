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
    setupLibrary(addScreen("Library"));
    setupQueue(addScreen("Queue", refreshQueue));
    Serial.println("[Boot] Connecting to WiFi...");
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(100); Serial.print("."); }
    Serial.printf("\n[Boot] WiFi connected, IP=%s\n", WiFi.localIP().toString().c_str());
    configTzTime(SECRET_TIMEZONE, "pool.ntp.org");
    setupVolumio();
    openLibraryRoot();
    refreshQueue();
    Serial.println("[Boot] setup() done");
}
void loop() {
    loopLVGL();
    loopVolumio();
    loopLibrary();
    loopQueue();
    static unsigned long last_clock = 0;
    if (millis() - last_clock > 1000) {
        updateTime();
        last_clock = millis();
    }
    // No timer here on purpose: it recomputes from real elapsed milliseconds every loop
    // iteration (cheap — it no-ops unless the displayed second actually changed), rather than
    // being gated by its own periodic tick that could end up phase-locked with another timer.
    tickPlaybackClock();
    static unsigned long last_heartbeat = 0;
    if (millis() - last_heartbeat > 10000) {
        Serial.printf("[Heartbeat] uptime=%lus wifi=%d heap=%u\n", millis() / 1000,
                      WiFi.status(), ESP.getFreeHeap());
        last_heartbeat = millis();
    }
    delay(5);
}
