#include "LvglHandler.h"
#include "UiHandler.h"
#include "TouchHandler.h"
#include "VolumioHandler.h"
#include "VolumioLibrary.h"
#include "VolumioQueue.h"
#include "VolumioArtists.h"
#include "VolumioArtistTracks.h"
#include "CustomFonts.h"
#include <WiFi.h>
#include "arduino_secrets.h"
void setup() {
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
    setupLVGL();
    setupTouch();
    setupUI();
    // Screens are built (on_build) and loaded (on_show) when first shown, and freed (on_hide)
    // when left, so only the visible one uses LVGL memory.
    setupLibrary(addScreen("Library", LV_SYMBOL_DIRECTORY, buildLibrary, refreshLibrary, hideLibrary));
    setupQueue(addScreen("Queue", LV_SYMBOL_LIST, buildQueue, refreshQueue, hideQueue));
    lv_obj_t* artistsScreen = addScreen("Artists", SYMBOL_USER, buildArtists, refreshArtists, hideArtists);
    setupArtistTracks(artistsScreen,
                       addHiddenScreen(buildArtistTracks, refreshArtistTracks, hideArtistTracks));
    // Must come after every addScreen() above, so the System tile is the grid's last.
    addSystemMenuActions();
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    while (WiFi.status() != WL_CONNECTED) delay(100);
    configTzTime(SECRET_TIMEZONE, "pool.ntp.org");
    setupVolumio();
}
void loop() {
    loopLVGL();
    loopVolumio();
    loopLibrary();
    loopQueue();
    loopArtists();
    loopArtistTracks();
    static unsigned long last_clock = 0;
    if (millis() - last_clock > 1000) {
        updateTime();
        last_clock = millis();
    }
    // Not on a timer: it recomputes from elapsed milliseconds and no-ops unless the second changed.
    tickPlaybackClock();
    delay(5);
}
