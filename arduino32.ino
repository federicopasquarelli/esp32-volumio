#include "LvglHandler.h"
#include "UiHandler.h"
#include "TouchHandler.h"
#include "VolumioHandler.h"
#include "VolumioLibrary.h"
#include "VolumioQueue.h"
#include "VolumioArtists.h"
#include "VolumioArtistTracks.h"
#include "TuyaLights.h"
#include <WiFi.h>
#include "arduino_secrets.h"
void setup() {
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
    setupLVGL();
    setupTouch();
    setupUI();
    // Every screen's widgets are built lazily (on_build) the first time it's actually shown, and
    // freed again (on_hide) when navigating away -- see CLAUDE.md item 29. Data loading
    // (on_show) follows the same lazy timing, so there's no need to pre-fetch Library/Queue here
    // at boot the way this used to (openLibraryRoot()/refreshQueue() were called explicitly
    // below; the first showScreen() to each now triggers them instead).
    setupLibrary(addScreen("Library", buildLibrary, refreshLibrary, hideLibrary));
    setupQueue(addScreen("Queue", buildQueue, refreshQueue, hideQueue));
    lv_obj_t* artistsScreen = addScreen("Artists", buildArtists, refreshArtists, hideArtists);
    setupArtistTracks(artistsScreen,
                       addHiddenScreen(buildArtistTracks, refreshArtistTracks, hideArtistTracks));
    setupTuyaLights(addScreen("Lights", buildLights, refreshTuyaLights, hideLights),
                     addHiddenScreen(buildBrightnessPage, refreshBrightnessPage, hideBrightnessPage));
    // Must come after every addScreen() above -- it appends Restart/Shut down as the grid's last
    // two tiles, so they land after all of these instead of wherever setupUI() itself ran.
    addSystemMenuActions();
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    while (WiFi.status() != WL_CONNECTED) delay(100);
    configTzTime(SECRET_TIMEZONE, "pool.ntp.org");
    setupVolumio();
    // Placed last so NTP (configTzTime above) has had a moment to sync in the background --
    // Tuya's request signing needs a roughly-correct clock. Still done eagerly at boot (unlike
    // the screens above) since it's just a small transient token refresh, not a widget tree or a
    // large permanent buffer -- see CLAUDE.md item 29.
    startTuyaAuth();
}
void loop() {
    loopLVGL();
    loopVolumio();
    loopLibrary();
    loopQueue();
    loopArtists();
    loopArtistTracks();
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
