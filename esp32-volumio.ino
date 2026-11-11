#include "LvglHandler.h"
#include "UiHandler.h"
#include "TouchHandler.h"
#include "VolumioHandler.h"
#include "BrowseScreen.h"
#include "VolumioQueue.h"
#include "VolumioArtists.h"
#include "VolumioArtistTracks.h"
#include "CustomFonts.h"
#include <WiFi.h>
#include "arduino_secrets.h"
// Folder browsers over /api/v1/browse, one per root. Each keeps its own folder and page.
static BrowseScreen library({ "music-library", "Library", 12, "Empty folder", true });
static BrowseScreen playlists({ "playlists", "Playlists", 2, "No playlists", false });
static BrowseScreen radio({ "radio", "Radio", 6, "No stations", false });

void setup() {
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
    setupLVGL();
    setupTouch();
    setupUI();
    // Screens are built (on_build) and loaded (on_show) when first shown, and freed (on_hide)
    // when left, so only the visible one uses LVGL memory.
    addScreen("Library", LV_SYMBOL_DIRECTORY,
              [](lv_obj_t* p) { library.build(p); }, [] { library.refresh(); }, [] { library.hide(); });
    setupQueue(addScreen("Queue", LV_SYMBOL_LIST, buildQueue, refreshQueue, hideQueue));
    lv_obj_t* artistsScreen = addScreen("Artists", SYMBOL_USER, buildArtists, refreshArtists, hideArtists);
    setupArtistTracks(artistsScreen,
                       addHiddenScreen(buildArtistTracks, refreshArtistTracks, hideArtistTracks));
    addScreen("Playlists", LV_SYMBOL_AUDIO,
              [](lv_obj_t* p) { playlists.build(p); }, [] { playlists.refresh(); }, [] { playlists.hide(); });
    addScreen("Radio", SYMBOL_RADIO,
              [](lv_obj_t* p) { radio.build(p); }, [] { radio.refresh(); }, [] { radio.hide(); });
    // Must come after every addScreen() above, so the System tile is the grid's last.
    addSystemMenuActions();
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    // Keep LVGL running while WiFi connects, so the loader animates instead of a blank screen.
    setLoadingStatus("Connecting to WiFi\u2026");
    while (WiFi.status() != WL_CONNECTED) { lv_timer_handler(); delay(5); }
    setLoadingStatus("Connecting to Volumio\u2026");
    configTzTime(SECRET_TIMEZONE, "pool.ntp.org");
    setupVolumio();
}
void loop() {
    loopLVGL();
    loopVolumio();
    library.loop();
    playlists.loop();
    radio.loop();
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
