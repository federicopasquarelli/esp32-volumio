#ifndef TUYA_LIGHTS_H
#define TUYA_LIGHTS_H

#include <lvgl.h>

// Registers the Lights screen's container and the per-light brightness page's container (a
// hidden screen, opened by long-pressing a light -- see addHiddenScreen()). Neither screen's
// widgets are built here -- wire buildLights()/refreshTuyaLights()/hideLights() and
// buildBrightnessPage()/refreshBrightnessPage()/hideBrightnessPage() as their respective
// addScreen()/addHiddenScreen() on_build/on_show/on_hide (see arduino32.ino).
void setupTuyaLights(lv_obj_t* parent, lv_obj_t* brightnessParent);
// Builds the Lights screen's widgets (the device button grid) into parent. Wire as addScreen()'s
// on_build.
void buildLights(lv_obj_t* parent);
// Tears the Lights screen's widgets back down and frees them from LVGL's memory pool. devices[]
// itself is kept (tiny, not an LVGL object) so a redraw doesn't always need a refetch. Wire as
// addScreen()'s on_hide.
void hideLights();
// Builds the per-light brightness/colour page's widgets (slider + palette) into parent. Wire as
// addHiddenScreen()'s on_build. This is the single largest permanent consumer of LVGL's static
// pool of any screen in the project when built (~7KB, see CLAUDE.md item 29) for a page opened
// rarely (long-press only), which is why it's lazy-built/freed like every other screen instead of
// once at setup().
void buildBrightnessPage(lv_obj_t* parent);
// Populates the brightness page for whichever device openBrightness() (device_cb()'s long-press
// handler) set as current. Wire as addHiddenScreen()'s on_show.
void refreshBrightnessPage();
// Tears the brightness page's widgets back down. Wire as addHiddenScreen()'s on_hide.
void hideBrightnessPage();
// Applies finished network results to the UI. Call from loop().
void loopTuyaLights();
// Reloads the device list (and each one's on/off state) from the Tuya Cloud API. Runs on a
// background task, wired as the Lights screen's on_show callback so it fires every time you open
// it.
void refreshTuyaLights();
// Pre-authenticates against the Tuya Cloud API in the background, so the first time the Lights
// screen is opened it only pays for the device list/status calls, not a login too. Call once,
// after WiFi/NTP are up. Safe to call even if the Lights screen is opened before this finishes --
// that just queues the device list refresh to run right after.
void startTuyaAuth();

#endif
