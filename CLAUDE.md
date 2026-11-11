# CYD Volumio Remote — context for contributors and AI assistants

User-facing setup is in [README.md](README.md). This file covers what isn't obvious from the code:
the architecture, the board's memory limits, and the quirks that cost time to find. The full
development history is in git (before the public release, this file held a long session log).

## What this is

An Arduino sketch for the ESP32-2432S028 ("Cheap Yellow Display": ILI9341 320x240 + XPT2046
touch, ESP32-WROOM-32, **no PSRAM**) that works as a Volumio remote and clock. LVGL 9 for the UI,
Arduino_GFX for the display, WebSockets + ArduinoJson 7 for Volumio's live state (Socket.IO,
Engine.IO v3), plain HTTP for on-demand requests (browsing, queue).

## Build

- **Use [arduino.sh](arduino.sh)** (`compile`, `flash`, `port-check`, `clean`, ...). It has the
  FQBN `esp32:esp32:esp32:PartitionScheme=huge_app` built in. The default partition scheme is too
  small (the sketch is ~50% of Huge APP, ~118% of the default).
- **ESP32 core `esp32:esp32@3.3.x`** (tested 3.3.11). The `4.0.0-alpha1` core's TLS is broken:
  every HTTPS handshake fails (it's mid-migration to a new crypto backend). The sketch doesn't use
  HTTPS today, but don't upgrade to the alpha.
- Tested libraries: lvgl 9.6.0, GFX Library for Arduino 1.6.8, XPT2046_Touchscreen 1.4,
  WebSockets 2.7.2, ArduinoJson 7.4.3.
- **`lv_conf.h` lives outside the repo** (`~/Arduino/libraries/lv_conf.h`, shared by every sketch
  on the machine). The maintainer's copy is still the LVGL 8.3 template, which happens to compile
  against 9.6 because unset options default sanely. If LVGL misbehaves, check for an option it
  never sets. `LV_MEM_SIZE` must stay at 48KB, see the memory section.
- **A link error with no source error** (`undefined reference to setup()`): a stale object in
  arduino-cli's cache, `~/.cache/arduino/sketches/<hash>/` (not the local `build/`). Seen once after
  two compiles overlapped. `./arduino.sh clean` fixes it.
- **Serial port**: no `Serial` logging in the code (removed on purpose; add `Serial.begin()` back
  to debug, then remove it). The maintainer usually has the Arduino IDE's serial monitor open on
  `/dev/ttyUSB0`, so uploads fail with "port busy". Ask before killing whatever holds the port
  (`./arduino.sh port-check` shows it).

## Hardware

- Display, HSPI: `SCK 14, MOSI 13, MISO 12, CS 15, DC 2, RST -1`. Backlight pin 21 (27 on some
  revisions), HIGH = on. Pins and screen size in [DisplayConfig.h](DisplayConfig.h).
- Touch, its own VSPI bus: `SCK 25, MOSI 32, MISO 39, CS 33`, no IRQ (polled).
  [TouchHandler.cpp](TouchHandler.cpp) maps `x: [200, 3700] -> [0, 320]`, `y: [240, 3800] -> [0, 240]`
  and drops readings with `z < 400` or x/y `<= 0` / `>= 8191` (8191 is a bus-contention read).
- **Colour byte order**: LVGL 9's RGB565 buffer is little-endian and the ILI9341 wants the high
  byte first, so the flush uses `draw16bitRGBBitmap()` (swaps). The `Be` variant sends it unswapped
  and every colour comes out wrong (the green accent turns violet). Colours in the code are shown
  as written.

## Credentials

`arduino_secrets.h` (gitignored) from [arduino_secrets.h.template](arduino_secrets.h.template):
`SECRET_SSID`, `SECRET_PASS`, `SECRET_TIMEZONE` (POSIX TZ), `SECRET_VOLUMIO_HOST`,
`SECRET_VOLUMIO_PORT`. Never commit real values or copy them into other files.

## File map

- [esp32-volumio.ino](esp32-volumio.ino): `setup()`/`loop()`, registers the screens.
- [LvglHandler.cpp](LvglHandler.cpp): LVGL init, flush, touch read, the 30s screen timeout
  (backlight off, switch to the player; the waking touch isn't passed on as a press).
- [TouchHandler.cpp](TouchHandler.cpp): XPT2046 reading and filtering.
- [UiHandler.cpp](UiHandler.cpp): header (clock, playback time, menu button), the player screen,
  the full-screen menu overlay (3x2 icon grid, System view, confirm dialog) and the screen
  registry (`addScreen()`/`addHiddenScreen()`/`showScreen()`).
- [VolumioHandler.cpp](VolumioHandler.cpp): the WebSocket, the reachability probe, playback and
  system commands.
- [VolumioApi.cpp](VolumioApi.cpp): REST helpers (`volumioUrl()`, `urlEncode()`, `streamQueue()`)
  and `runQueueAction()` (Play / Add to queue / Clear and play).
- [QueueActionMenu.cpp](QueueActionMenu.cpp): the long-press menu shared by Library and Artists,
  and `startQueueAction()`, the background task every "play this" goes through (one at a time).
- [VolumioLibrary.cpp](VolumioLibrary.cpp), [VolumioArtists.cpp](VolumioArtists.cpp),
  [VolumioArtistTracks.cpp](VolumioArtistTracks.cpp), [VolumioQueue.cpp](VolumioQueue.cpp): the
  list screens.
- [PaginationNav.cpp](PaginationNav.cpp): the bottom-bar buttons and page label every list uses.
- [ListStyle.cpp](ListStyle.cpp): `styleListRow()`/`styleListText()`, the row look of every list.
  **New lists should use both.**
- [CustomFonts.h](CustomFonts.h) + `src/fonts/`: Montserrat with a wider glyph range, see below.

## Architecture

**Screens are built on show and freed on hide.** `addScreen(name, icon, on_build, on_show,
on_hide)` creates an empty container and a menu tile. `showScreen()` runs the old screen's
`on_hide` (which `lv_obj_clean()`s its container and nulls its widget pointers) and the new one's
`on_build` then `on_show`. Small state (current folder, page) survives a hide, so reopening
Library or Artists returns to the same place; ArtistTracks always starts on page 1. The player
and the menu overlay are built once and never freed: they're visited constantly and cheap.
`addSystemMenuActions()` must run after every `addScreen()` so System is the last tile.

**Network work runs in FreeRTOS tasks; the UI thread only applies results.** Each list screen
has a `fetchTask` that writes into static buffers and sets a `done` flag; `loopX()` (called from
`loop()`) renders it. Two rules every screen follows:
- Check `xTaskCreatePinnedToCore()`'s return. If the stack doesn't fit, fall back to the error
  UI, otherwise the "busy" flag stays set forever and the screen is stuck on "Loading...".
- In `loopX()`, if the screen's widgets are NULL (hidden while the fetch was in flight), drop
  the result. `on_show` fetches again next time.

**Only one page is ever in memory.** Lists show 4 rows per page and fetch one page per turn.
`/api/v1/browse` takes `offset`/`limit`, and when they're passed its list includes `count`, the
real total (absent without them, so the code falls back to "a full page means there's more").

**The UI must not block when Volumio is down.** `WebSocketsClient::loop()` reconnects with a
blocking `connect()` (DNS/mDNS plus up to 5s) on the calling thread, which is also LVGL's. So
`loopVolumio()` only calls `ws.loop()` while connected or when `probeTask` (a background TCP
connect to Volumio's port) has just succeeded; `WStype_DISCONNECTED` clears that flag. The
"Volumio is unreachable" cover appears after 3s down. After Restart/Shut down, reconnecting is
held off for 30s, because Volumio's port stays open for a few seconds while it goes down and the
reconnect would block. The sketch never sends Engine.IO pings; if the offline notice ever flickers
on a healthy server, look there. `setup()` still blocks until WiFi connects.

**The player redraws only what changed.** Volumio sends a full pushState for every change, and
re-setting a scrolling label's text restarts its scroll, so `setLabelIfChanged()` and the
`shown_*` state guard every update. The elapsed time is counted locally from the last pushState
and recomputed from `millis()` every loop iteration (`tickPlaybackClock()`), not on its own
timer: two `millis()`-gated timers that reset together stay phase-locked.

**AirPlay**: detected by `service == "airplay_emulation"` (title/artist/album arrive empty), not
by the artist text. Play/prev/next are disabled and the artist reads "Airplay".

## Volumio API notes (checked against a live server)

- Live state: WebSocket `/socket.io/?EIO=3&transport=websocket`, messages `42[...]`. `pushState`
  mirrors `GET /api/v1/getState`.
- WebSocket only, no REST equivalent: `removeFromQueue` `{value: index}`, `updateDb` (no payload
  = whole library, a URI = one folder), `reboot`, `shutdown` (from the backend source,
  `app/plugins/user_interface/websocket/index.js`; not in Volumio's docs). There's no event to
  restart only the service, so "Restart" reboots the device. **Never test shutdown against a real
  device**: it has to be switched back on by hand.
- REST: `addToQueue`, `replaceAndPlay`, `commands/?cmd=play&N=<index>`, `cmd=clearQueue`. There's
  no "add and play": Play reads the queue length, adds, then plays from that index.
- **`getQueue` ignores every paging parameter** and returns the whole queue (233KB for 330
  items). `streamQueue()` walks the array element by element, parses only the wanted range and
  skips the rest with a string/escape-aware brace counter. Never `deserializeJson()` the whole
  response.
- `artists://` browses like `music-library` (same offset/limit/count). `artists://<name>` returns
  two lists, "Albums (...)" and "Tracks (...)", each paged independently; only Tracks is used,
  found by title prefix. `addToQueue` with an artist URI queues all their tracks; artist entries
  have no `service`, so `"mpd"` is sent.
- **URIs Volumio returns are already percent-encoded** (`artists://Arctic%20Monkeys`). Use them
  as-is; `urlEncode()` would double-encode them. Only hand-built URIs need encoding.

## Memory (the main constraint)

No PSRAM: LVGL's pool, static data, WiFi and the network stack share ~320KB of SRAM.

- **LVGL's pool is invisible to heap checks.** With `LV_MEM_CUSTOM 0`, LVGL allocates from its
  own static `LV_MEM_SIZE` array, not the heap `ESP.getFreeHeap()` reports. When it runs out,
  `LV_ASSERT_HANDLER` (`while(1);`) hangs the UI silently: no crash, no reset. A freeze with no heap
  symptom is this pool first. Use `lv_mem_monitor()` to see it.
- **Don't tune `LV_MEM_SIZE`; reduce what's in the pool.** 48KB stays. A bigger pool comes straight
  off the heap's largest free block (which HTTPS needed), and `LV_MEM_CUSTOM 1` let LVGL fragment
  the general heap. Lazy-building screens is what fixed it: when every screen existed from boot the
  pool was 54-88% used before any navigation.
- **Theme transitions leak into the pool under fast taps.** The default theme attaches its own
  style objects with ~80ms fade transitions to every button, and `obj_transition_states()` sums the
  transitions of every attached style, so a local override doesn't cancel them (and a NULL
  transition descriptor crashes: LVGL dereferences it without checking). Rapid paging piled up
  transitions until the pool ran out. `createPagerButton()` calls `lv_obj_remove_style_all()`, then
  sets size (a style property, so it must come after) and radius. Do the same for any button
  whose state flips often.
- **The static DRAM segment (`dram0_0_seg`) is nearly full.** An 8KB or 12.8KB `static` array
  overflows it at link time. Default to `malloc()` for buffers over a few KB (the display buffer
  is heap-allocated for this reason).
- **Heap numbers can mislead.** Use `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`, not
  `ESP.getFreeHeap()`. Fragmentation matters more than the largest-block figure: the same reading
  has preceded both a working and a failing TLS handshake. Big buffers reserved before
  `WiFi.begin()` can stop WiFi from connecting at all.

## Fonts

LVGL's stock Montserrat covers ASCII 32-126 plus its icons only, but Volumio metadata has accented
letters and typographic punctuation. `src/fonts/font_montserrat_ext_{12,14,18}.c` are regenerated
with `lv_font_conv` from LVGL's own sources (`lvgl/scripts/generators/built_in_font/`) with Latin-1
Supplement, U+2010-2015 (includes the figure dash U+2012, which does show up in titles), U+2212,
curly quotes, bullet and ellipsis added. The exact command is in each file's "Opts:" header. Use
`lv_font_montserrat_ext_NN` everywhere, and set the font explicitly on any container showing
metadata (the default font is still the stock ASCII one). `font_montserrat_ext_32.c` is a subset
for the menu icons: digits, `%`, space and a few icons. Regenerate it before using it for text.
`SYMBOL_USER` (U+F007) is only in ext_32.

## Tried and abandoned: album art

Tried twice and reverted both times; the player has no art slot.
1. `/tinyart/<artist>/<album>/small` worked for local files, but that endpoint has broken HTTP
   framing (no `Content-Length`, not chunked, claims keep-alive but closes): `HTTPClient` reads
   0 bytes, a raw `WiFiClient` reading until disconnect works. Adding HTTPS plugin art plus a JPEG
   decoder then broke WiFi outright (early buffer reservations starved it).
2. pushState's own `albumart` field (local files resolve to a well-formed `/albumart?...`, plugin
   art to an external `https://` URL), decoded with JPEGDEC into a 64x64 heap buffer. Compiled,
   but didn't work on the device; the failure was never captured. A third attempt should start
   by logging the actual failure.
