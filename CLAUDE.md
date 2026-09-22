# CYD Volumio Smart Clock — Claude context

Read this before working on the project. For hardware pins/touch calibration see [GEMINI.md](GEMINI.md);
for user-facing setup instructions see [README.md](README.md). This file is session history and
things that aren't obvious from the code.

## What this is

ESP32-2432S028 ("Cheap Yellow Display", ILI9341 320x240 + XPT2046 touch) running an Arduino sketch
that acts as a Volumio remote and clock. LVGL for UI, Arduino_GFX for the display, WebSockets +
ArduinoJson for Volumio's realtime state, plain HTTP for on-demand requests (library browsing,
queue management, album art).

Board used for testing: connected via USB at `/dev/ttyUSB0` (CH341 adapter), FQBN
`esp32:esp32:esp32:PartitionScheme=huge_app` — **the default partition scheme is too small**
(sketch is ~50% of flash with huge_app, but 118%+ of the default scheme), always compile/upload
with `PartitionScheme=huge_app` or it'll fail to fit.

Volumio host for dev: `volumio.local:3000` (see `arduino_secrets.h`, gitignored).

## File map

- [arduino32.ino](arduino32.ino) — `setup()`/`loop()`, wires all modules together.
- [LvglHandler.cpp](LvglHandler.cpp) — display flush + touch read callbacks, LVGL init. Also owns
  the screen-timeout/backlight-off-after-3-minutes logic and wake-on-touch.
- [TouchHandler.cpp](TouchHandler.cpp) — XPT2046 touch reading, noise filtering.
- [UiHandler.cpp](UiHandler.cpp) — top bar (clock, dropdown menu / back button), the player screen
  (art, title/artist/album, transport buttons, volume), and `addScreen()`, the mechanism Library/
  Queue use to register a full-screen page reachable from the dropdown.
- [VolumioHandler.cpp](VolumioHandler.cpp) — the WebSocket connection to Volumio
  (`/socket.io/?EIO=3&transport=websocket`), playback control commands.
- [VolumioLibrary.cpp](VolumioLibrary.cpp) — music library browser (folders), HTTP polling via
  `/api/v1/browse`, paginated list UI, long-press context menu (play / queue / clear+play /
  update folder).
- [VolumioQueue.cpp](VolumioQueue.cpp) — queue viewer/editor, HTTP via `/api/v1/getQueue` +
  `/api/v1/commands/?cmd=...`, per-item remove button.
- [VolumioArt.cpp](VolumioArt.cpp) — album art fetch + display. **Currently being debugged, see
  [DEBUG_ALBUM_ART.md](DEBUG_ALBUM_ART.md).**
- [DisplayConfig.h](DisplayConfig.h) — pins, screen dims, shared constants (`COLOR_ACCENT`,
  `TOP_BAR_H`, `SCREEN_TIMEOUT_MS`).

## Session history (chronological, most recent last)

1. **Library rewrite → HTTP polling.** Removed an earlier in-progress library implementation,
   rebuilt it using `/api/v1/browse` over HTTP (not the websocket) for folder navigation, with
   pagination (4 rows/page) because rendering the whole folder at once froze the UI, and back-page
   memory (going back returns to the page you left, per folder level).
2. **LVGL 8 → 9 + ArduinoJson 7 migration.** User updated libraries (lvgl 9.6.0, GFX 1.6.8,
   ArduinoJson 7.4.3); ported all the LVGL 8 API calls (`lv_tabview_create` signature, `lv_btn_*`
   → `lv_button_*`, display/indev driver structs → `lv_display_t`/`lv_indev_t`, `JsonDocument`
   instead of `DynamicJsonDocument`/`StaticJsonDocument`, etc). **`~/Arduino/libraries/lv_conf.h`
   is still literally the LVGL 8.3.11 template file** (outside this repo, shared by all sketches
   on this machine) — it happens to compile against LVGL 9.6 because unset options default
   sanely, but if something LVGL-related misbehaves, check whether it's an `lv_conf.h` option
   this template never set.
3. **Long-press context menu on folders** (play / add to queue / clear+play / update folder).
   Initially wired to Volumio's WebSocket events, then **moved to HTTP** (`/api/v1/addToQueue`,
   `/api/v1/replaceAndPlay`, `commands/?cmd=play&N=`) per user request for "better performance" —
   `updateDb` (folder rescan) has no known REST equivalent, stayed on the websocket.
4. **Queue tab**: shows the queue paginated, tap-to-play, per-row remove button (removal is
   websocket-only, `removeFromQueue` has no known REST route either).
5. **Tabview → dropdown navigation.** Removed LVGL's tabview entirely (no more swipe-to-switch).
   Replaced with: a single always-visible top bar (clock top-left, one button top-right that's
   *either* a dropdown menu — Library/Queue — when on the player screen, *or* a Back button when
   on any other screen, never both). `addScreen(name, on_show)` in `UiHandler.cpp` is the
   generic mechanism: creates a hidden full-screen container, registers it, adds its dropdown
   entry. `on_show` (optional) fires every time that screen becomes visible — used by Queue to
   refresh itself.
6. **Player screen redesign** to match a reference screenshot: album art placeholder (top-left)
   + title/artist/album to its right, elapsed/duration as plain text (no progress bar — never
   asked for), big round play/pause button centered with prev/next flanking it and
   shuffle/repeat further out, volume slider + numeric readout at the bottom. Mute button
   removed per request (kept `mute()`/`unmute()` in `VolumioHandler.cpp` unused, in case it
   comes back). Repeat was accidentally dropped once, then restored next to Next.
7. **Screen timeout**: backlight off after `SCREEN_TIMEOUT_MS` (3 min, `DisplayConfig.h`) with no
   touch; first touch after sleep just wakes the screen, doesn't also act as a press
   (`LvglHandler.cpp`, `my_touch_read`).
8. **Album art** (working, confirmed live on-device — full findings/history in
   [DEBUG_ALBUM_ART.md](DEBUG_ALBUM_ART.md)). Volumio's main `/albumart` endpoint serves the
   original file, which for local-library tracks turned out to be a 1000x1000 **progressive**
   JPEG — undecodable by any lightweight ESP32 decoder. Uses Volumio's
   `/tinyart/<artist>/<album>/small` instead (34x34 PNG, scaled up to 64x64 via
   `lv_image_set_scale()`). Enabled `LV_USE_LODEPNG` in the global `lv_conf.h` to decode it.
   `HTTPClient` couldn't read `/tinyart`'s response at all (no Content-Length, not chunked) —
   `VolumioArt.cpp` talks to a raw `WiFiClient` instead. The download/display buffers are
   `heap_caps_malloc`'d **once**, in `initAlbumArtMemory()`, called from `setup()` before
   `WiFi.begin()` — allocating fresh per track change was intermittently failing (this board has
   no PSRAM and the heap fragments once WiFi/Library/Queue are running); reserving early while
   the heap's still pristine fixed it (verified 0 allocation failures across 7 sampled real track
   changes, vs. ~2/3 failing before). One accepted, unfixed coverage gap: some albums' art comes
   back as JPEG (sometimes progressive) from Volumio's own thumbnailer rather than PNG — those
   never show real art here, by design (only PNG is decoded).

## Known quirks / gotchas worth remembering

- **`/tinyart` has broken HTTP framing**: no `Content-Length`, not chunked, `Connection:
  Keep-Alive` claimed but the socket is actually closed after the body. `HTTPClient` on ESP32
  can't handle this (silently reads 0 bytes) — `VolumioArt.cpp` talks to a raw `WiFiClient`
  instead and reads until disconnect, with a timeout/size cap as a safety net.
- **ESP32 heap capability mismatch**: `ESP.getFreeHeap()`/`ESP.getMaxAllocHeap()` (and plain
  `malloc()`) don't necessarily reflect what's actually available for a generic byte buffer —
  some free RAM may only be usable for other capabilities (e.g. IRAM/execute). Use
  `heap_caps_malloc(size, MALLOC_CAP_8BIT)` + `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`
  for trustworthy numbers when debugging allocation failures.
- **Colors look swapped on this display**: `COLOR_ACCENT` is defined as `0x1DB954` (green) but
  renders as blue on the physical screen. Never chased down why (likely a BGR/RGB or byte-order
  quirk in the `draw16bitBeRGBBitmap` path in `LvglHandler.cpp`); the user is fine with it, so it
  hasn't been touched. Don't "fix" this without asking — the accent color choice downstream
  assumes it renders blue.
- **Serial port**: the user runs Arduino IDE with its own Serial Monitor attached to
  `/dev/ttyUSB0` most of the time. Don't kill that process to grab the port without asking first
  — did this once already and it was disruptive. If you need serial output, either ask the user
  to paste what their monitor shows, or ask before taking the port.
- **`arduino-cli upload` fails with "port busy"** whenever Arduino IDE's monitor (or anything
  else) has the port open — same reasoning as above, ask first.
