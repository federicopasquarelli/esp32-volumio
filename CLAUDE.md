# CYD Volumio Smart Clock — Claude context

Read this before working on the project. For user-facing setup instructions see
[README.md](README.md). Hardware pins/touch calibration and how secrets are configured are in the
two sections right below; the rest of this file is session history and things that aren't obvious
from the code.

## What this is

ESP32-2432S028 ("Cheap Yellow Display", ILI9341 320x240 + XPT2046 touch) running an Arduino sketch
that acts as a Volumio remote and clock. LVGL for UI, Arduino_GFX for the display, WebSockets +
ArduinoJson for Volumio's realtime state, plain HTTP for on-demand requests (library browsing,
queue management, album art).

Board used for testing: connected via USB at `/dev/ttyUSB0` (CH341 adapter), FQBN
`esp32:esp32:esp32:PartitionScheme=huge_app` — **the default partition scheme is too small**
(sketch is ~50% of flash with huge_app, but 118%+ of the default scheme), always compile/upload
with `PartitionScheme=huge_app` or it'll fail to fit. ESP32 core: pinned to **`esp32:esp32@3.3.11`**
— do not upgrade to the `4.0.0-alpha1` core, its TLS stack is broken (see quirks below).

Volumio host for dev: `volumio.local:3000`. Tuya Developer Platform credentials (client ID/key)
also live in `arduino_secrets.h`, gitignored.

## Hardware

- **Board**: ESP32-2432S028 (Cheap Yellow Display v2/v3), no PSRAM (ESP32-WROOM-32).
- **Display**: ILI9341 2.8" SPI, 320x240.
  - Bus: HSPI, `SCK:14, MOSI:13, MISO:12, CS:15, DC:2, RST:-1` (`DisplayConfig.h`).
  - Backlight: pin 21 (primary) / 27 (alternate on some board revisions); HIGH = on.
- **Touch**: XPT2046 resistive, on its own VSPI bus: `SCK:25, MOSI:32, MISO:39, CS:33`, `IRQ:-1`
  (polling mode, no interrupt line used).
  - Mapping (`TouchHandler.cpp`): `x: [200, 3700] -> [0, 320]`, `y: [240, 3800] -> [0, 240]`.
  - Noise filter: ignore readings where `p.x`/`p.y` is `<= 0` or `>= 8191` (`8191` is what a bus
    contention read looks like) and require pressure `p.z > 400`.
  - Screen dimensions and pins live in `DisplayConfig.h`, the touch code in `TouchHandler.cpp`.

## Credentials / secrets

WiFi, Volumio and Tuya settings live in `arduino_secrets.h`, which is **gitignored**. To set up a
local environment, copy `arduino_secrets.h.template` and fill in: `SECRET_SSID`, `SECRET_PASS`,
`SECRET_TIMEZONE`, `SECRET_VOLUMIO_HOST`, `SECRET_VOLUMIO_PORT`, `SECRET_TUYA_CLIENT_ID`,
`SECRET_TUYA_CLIENT_KEY`. Never commit real values or paste them into other files.

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
- [PaginationNav.cpp](PaginationNav.cpp) — `createPagerButton()`/`setPagerEnabled()`, the styled
  nav button (Prev/Next/Up/Clear) shared by Library and Queue's bottom bars -- see item 28.
- [VolumioArtists.cpp](VolumioArtists.cpp) — Artists screen (dropdown entry, via `addScreen`).
  Currently just a "Coming soon" placeholder — see item 24.
- [DisplayConfig.h](DisplayConfig.h) — pins, screen dims, shared constants (`COLOR_ACCENT`,
  `TOP_BAR_H`, `SCREEN_TIMEOUT_MS`).
- [CustomFonts.h](CustomFonts.h) + `font_montserrat_ext_{12,14,18,32}.c` — Montserrat fonts
  regenerated with a wider glyph range than LVGL's stock ones (accented letters, smart
  punctuation); see the quirks section below for why these exist and how to regenerate/resize
  them.
- [TuyaLights.cpp](TuyaLights.cpp) — "Lights" screen (dropdown entry, via `addScreen`).
  Authenticates against the Tuya Cloud API (`openapi.tuyaeu.com`, Simple mode signing), lists up
  to 5 devices (`/v2.0/cloud/thing/device?page_size=5`) with their live switch state
  (`GET .../status`, code `switch_led`) on screen-open, and tapping one toggles it
  (`POST .../commands`); long-pressing one opens a hidden per-light brightness page (slider +
  Back button). No unlink/remove action, by design.

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
   removed per request. Repeat was accidentally dropped once, then restored next to Next.
7. **Screen timeout**: backlight off after `SCREEN_TIMEOUT_MS` (3 min, `DisplayConfig.h`) with no
   touch; first touch after sleep just wakes the screen, doesn't also act as a press
   (`LvglHandler.cpp`, `my_touch_read`).
8. **Album art, first attempt: built, worked, then reverted to a placeholder.** Kept here (rather
   than deleted) because it explains a design choice item 17 made later. Got real art working via
   Volumio's `/tinyart/<artist>/<album>/small` for local files. Then extended it to also fetch
   plugin-sourced art (Spotify etc, over HTTPS with a JPEG decoder) — **that extension broke WiFi
   connectivity outright** (see quirks below). Per explicit user instruction, all of it was
   reverted, including the previously-working `/tinyart` part, back to a plain static placeholder.
   Re-attempted later, see item 17 — this history is why it wasn't just redone the same way.
9. **UI polish pass**: longer/thinner volume slider; playback time changed from two separate
   labels to one zero-padded `"03:04 / 04:30"` label, moved into the shared top header (visible
   from Library/Queue too, not just the player), centered between the clock and the dropdown/back
   button. Getting the elapsed time itself to tick correctly took a few tries — see the timer
   quirk below.
10. **Tuya Lights screen started.** Added an empty "Lights" screen + dropdown entry
    (`TuyaLights.cpp`), then implemented Tuya Cloud API authentication (`GET
    /v1.0/token?grant_type=1`, HMAC-SHA256 "Simple mode" signing per Tuya's docs) on screen-open.
    Signing was correct from the start; what actually blocked it was the ESP32 core version — see
    the TLS quirk below. Downgraded the installed core from `esp32:esp32@4.0.0-alpha1` to the
    stable `@3.3.11` to fix it. Once auth worked, added the device list
    (`/v2.0/cloud/thing/device?page_size=5`), then on/off control: the list endpoint turned out to
    have no live status, so each device's switch state is fetched separately
    (`GET /v1.0/iot-03/devices/{id}/status`, code `switch_led` -- confirmed by calling the real
    API read-only from a throwaway script rather than trusting Tuya's docs, which were
    inconsistently rendered when fetched). Tapping a row sends
    `POST /v1.0/iot-03/devices/{id}/commands` with that same code and updates the row from the
    response, not optimistically.
11. **Library switched from "fetch whole folder" to "fetch per page."** Chasing the TLS memory
    failure above (`SSL - Memory allocation failed`, 13.8KB largest free block vs the ~32KB TLS
    needs) surfaced that `VolumioLibrary.cpp` reserved a static 200-item array (~49KB) at boot,
    just to paginate 4 items/page client-side out of something already fully downloaded — heap
    fragmentation from that array was most of the deficit. Confirmed Volumio's `/api/v1/browse`
    actually supports `offset`/`limit` query params (tested directly against `volumio.local`), so
    Library now does one HTTP fetch per page turn instead: `items[]` shrank to a 4-slot static
    array (no more heap `new[]`), and `has_more` (was the last fetch a full page?) replaces the
    old total-based `pageCount()` for enabling the Next button — this was a false positive right
    when a folder's item count was an exact multiple of 4 (Next loaded one harmless empty page).
    **Fixed properly later, see item 25.**
12. **Lights UI**: devices became two big (130x130) centered buttons instead of list rows —
    accent-filled when on, black with an accent border when off (same on/off language as the
    player's shuffle/repeat buttons, `styleDeviceButton` in `TuyaLights.cpp`). Tapping a light
    while another request is still in flight no longer drops the tap: it's queued
    (`pending_index`) and fires automatically once the in-flight one finishes, so flipping two
    lights in quick succession doesn't need a second tap — but it's still one request at a time on
    purpose (this core's TLS needs a big contiguous heap block, see the TLS quirk below).
13. **Back button removed.** The dropdown menu button is now always visible on every screen (used
    to be player-only, with a Back button replacing it everywhere else) so you can jump straight
    between Library/Queue/Lights without detouring through the player. The player itself got a
    "Player" entry in the dropdown (`addMenuEntry()` in `UiHandler.cpp`, added right after
    `cont_player` in `setupUI()`) to replace what Back used to do.
14. **Tuya token refresh.** Access tokens last 2h; `TuyaLights.cpp` now holds onto the
    `refresh_token` from login and, once the cached access token is stale, calls
    `GET /v1.0/token/{refresh_token}` (no `grant_type`, no `access_token` header) instead of
    logging in from scratch — falling back to a full login only if that fails. Tuya's refresh
    tokens are single-use, so every response (login or refresh) overwrites the stored one.
    Separately, every authenticated call now goes through `tuyaCall()`, which retries once with a
    forced-fresh token if Tuya answers with HTTP 401 — a safety net for a token going stale
    between calls, not the main renewal path (that's `ensureToken()`'s expiry check, which is why
    opening the Lights screen normally makes zero auth calls at all).
15. **All `Serial.*` logging removed project-wide** (including `Serial.begin()`), per explicit
    request. If you need runtime visibility again — e.g. to debug a new Tuya failure mode — you'll
    need to re-add both. **Temporarily reinstated** while chasing the Library "stuck loading" bug
    (see item 25's follow-up): `Serial.begin(115200)` in `arduino32.ino`, a 5s heartbeat in
    `loop()` (to tell "loop() itself is blocked" apart from "just Library's fetch state is stuck"),
    and `[Lib]`-prefixed logging through `VolumioLibrary.cpp`'s whole fetch path (`startFetch()`,
    `fetchTask()`, `loopLibrary()`, `showError()`) covering heap headroom
    (`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`, the trustworthy one per the heap quirk
    below, not `ESP.getFreeHeap()` alone), HTTP result code, deserialization result, and the
    parsed `item_count`/`total_count`/`has_more`. Strip all of it back out once the bug is found.
16. **Tuya pre-auth at boot.** `startTuyaAuth()` runs the login in the background right after
    `setup()`'s WiFi/NTP work (placed last, so NTP has had a moment to sync -- signing needs a
    roughly-correct clock), so opening the Lights screen for the first time only pays for the
    device list/status calls, not a login too. Uses the same single-in-flight task machinery as
    everything else in `TuyaLights.cpp` (`fetching`/`current_op = OP_AUTH`) rather than a separate
    path, and if the screen is opened while that pre-auth is still running, the resulting list
    refresh isn't dropped -- it's queued (`pending_list_refresh`) and runs right after, same
    pattern as the existing queued-toggle (`pending_index`).
17. **Album art, second attempt: also built, also reverted.** Not in the codebase now — kept here
    so a third attempt doesn't repeat the same dead ends. This time the URL came from Volumio's
    own pushState `albumart` field (verified live against the instance -- `curl
    .../api/v1/getState` mirrors pushState exactly) instead of hand-building a `/tinyart/...` one:
    local files turned out to resolve to a *different*, well-formed endpoint
    (`/albumart?web=...&path=...`, confirmed with a real `Content-Length` header via `curl -I`),
    sidestepping the broken-framing `/tinyart` quirk noted below entirely, and Spotify/SoundCloud
    plugin art came through the same field as a plain external `https://` URL. Decoded with the
    JPEGDEC library (bitbank2) straight from a RAM buffer into a 64x64 RGB565 image.
    - **It did fail to compile at first**, but not from anything logically wrong: a fixed-size
      `static uint16_t pixels[64*64]` (8KB) overflowed the chip's fixed-size static DRAM segment
      at link time (`dram0_0_seg overflowed`) -- this chip's static-RAM budget was already tight
      before adding it. Fixed by `malloc()`-ing that buffer once in `setupAlbumArt()` instead of
      declaring it `static`, moving it to the heap (which had plenty of room left after item 11's
      fix) -- worth remembering generally: a "small, one-time, fixed-size" buffer isn't
      automatically safe as a `static` array on this chip; heap is the safer default here unless
      there's a specific reason (like Library's per-page buffer) to want it static.
    - **It compiled clean and presumably didn't hit the old "broke WiFi outright" failure mode**
      (the two causes found earlier this session for similar HTTPS symptoms -- Library's old
      49KB array, and the alpha core's broken TLS -- were both already fixed by this point) **but
      still "didn't work" on real hardware**, per the user, who asked for a full revert without
      further diagnosis. The exact on-device failure mode this time is unknown -- neither the
      Serial log nor the specific symptom was captured before reverting.
    - JPEGDEC (and its `bb_spi_lcd` dependency) was uninstalled again since nothing references it.
18. **Album art placeholder removed too, not just the fetching.** After the second revert, the
    static placeholder box itself (`VolumioArt.cpp`/`.h`, the music-note-on-accent-square) was
    deleted from the project outright, per explicit request — the player screen has no album art
    slot at all now. `UiHandler.cpp`'s track info (now-playing label, title/artist/album) moved
    from starting at x=78 (to the right of where the art box used to be) to x=8, left-aligned
    against the same margin the rest of the header uses, and widened from 226px to 296px to use
    the freed horizontal space.
19. **UI no longer freezes when Volumio is unreachable.** With the Raspberry off, the whole UI
    (including the dropdown to reach Lights/Library/Queue) was effectively dead. Cause, confirmed
    in the library source (`WebSocketsClient.cpp`, `loop()`): while not connected it retries with
    a *blocking* `connect()` — DNS/mDNS lookup of `volumio.local` plus up to
    `WEBSOCKETS_TCP_TIMEOUT` (5s) — on the caller's thread every 500ms, and that caller is the
    Arduino `loop()` that also runs LVGL and the touch reader. `VolumioHandler.cpp` now runs a
    small `probeTask` that periodically checks (off the main loop) whether Volumio's port accepts
    a TCP connection, and `loopVolumio()` only calls `ws.loop()` when the socket is already
    connected or the probe has just said a connect will be quick; a `WStype_DISCONNECTED` event
    clears the flag so a dropped connection doesn't go straight back to blocking on a dead host.
    The player screen (whose widgets are only created lazily on the first pushState, so it used
    to just sit blank) now shows a "Volumio is unreachable" notice: `cont_offline` in
    `UiHandler.cpp`, an opaque cover over the whole player area (so stale controls are hidden and
    can't be tapped; the header/dropdown stay usable), driven by `setVolumioOffline()` from
    `loopVolumio()` once the socket has been down for 3s straight (`OFFLINE_NOTICE_DELAY_MS`, so a
    quick drop-and-reconnect or the first moments after boot don't flash it). Untested against
    the real server's behavior for an idle, pingless client -- this sketch never sends the
    Engine.IO v3 pings the server may expect, so if Volumio turns out to drop the connection
    periodically the probe's ~250ms reaction time and the 3s grace are what keep that invisible;
    if the notice ever flickers on a healthy Pi, look there first. Unchanged and separate:
    `setup()` still blocks forever in `while (WiFi.status() != WL_CONNECTED)` if *WiFi* itself
    is unavailable.

20. **Per-light brightness page** (long-press a light on the Lights screen). A hidden screen — new
    `addHiddenScreen()` in `UiHandler.cpp`, registered like `addScreen()` but with no dropdown
    entry, plus `showScreen()` now exported so `TuyaLights.cpp` can navigate to/from it — holding
    the light's name, a big percent readout, a thick slider and a Back button. Long-press vs tap
    uses the same `PRESSED`/`LONG_PRESSED`/`CLICKED` + `long_press_handled` pattern as Library
    (LVGL still sends CLICKED on release after a long press). The slider only sends a command on
    release (`RELEASED`/`PRESS_LOST`), not per drag step, and goes through the same
    one-request-at-a-time queue (`pending_bright_index`, last release wins). Back redraws the
    Lights list from local state (`returning_from_brightness`) instead of reloading over the
    network, since `showScreen()` would otherwise fire `refreshTuyaLights()` (the Lights screen's
    `on_show`). What the API is, per Tuya's official docs (standard instruction set for lights,
    category `dj`) and this project's two Extrastar A60 bulbs' own `/specifications`:
    `bright_value_v2` is an integer 10..1000; `colour_data_v2` is sent as an **object**
    `{"h","s","v"}` (h 0..360, s/v 0..1000) even though status returns it as a JSON *string*;
    `work_mode` is `white`/`colour`/`scene`/`music`. **The docs do not say** which code governs
    brightness in which mode, nor whether either turns an off light on, so `sendBrightness()`
    follows what each code is for (colour mode → `colour_data_v2.v` with h/s resent unchanged,
    otherwise `bright_value_v2`; slider percent × 10 either way) and, for a light that's off, adds
    `switch_led: true` to the same request. **Untested on hardware** — verify both bulbs (one
    was in `colour`, one in `white` mode when checked). Also: while researching this, same-value
    writes (bright/colour set to their current values, lights off, state read back unchanged)
    were sent to the real bulbs to check the request format was accepted — the user preferred
    working from the official docs, so don't probe live devices with commands again unasked.
    **Colour palette + thinner slider (follow-up):** the page also has twelve round swatches (eleven
    full-saturation hues plus white, two rows of six, 12px apart) that change the light's colour, and the
    slider went from 18px to 8px tall (knob padded out to stay grabbable). The header row now
    holds Back, the name and the big percent readout. A swatch sends `work_mode` explicitly
    (`colour`, or `white`) together with `colour_data_v2 {h, s:1000, v: slider*10}` (white:
    `bright_value_v2` instead), plus `switch_led: true` if the light is off — the docs are silent
    on whether `colour_data_v2` alone switches a white-mode bulb over, so it doesn't rely on that.
    Goes through the same single-in-flight queue (`pending_colour_*`, last tap wins; runs after a
    pending brightness). The selected swatch (white border) follows the light's real state and
    snaps back if a command fails. **Untested on hardware**, like the rest of this page.

21. **Menu is now a full-screen overlay.** The old 150px dropdown panel under the toggle became
    `menu_list` in `UiHandler.cpp`: a 320x240 opaque overlay (covers the header too) with a "Menu"
    title, a close button (X) in the same top-right spot as the toggle that opens it, and
    `menu_grid`, a two-column flex-wrap grid of 146x84 tiles (`addMenuEntry()`); more than four
    entries make the grid scroll. Deliberately an **overlay, not a screen** (asked "which is
    better?"): closing it only hides it, so the screen underneath is untouched — a menu that were
    a registered screen would need a "previous screen" memory and `showScreen(previous)` would
    re-fire that screen's `on_show` (Lights/Queue reloading over the network just because the menu
    was opened and closed, brightness page restarting, etc.). Picking a tile still goes through
    `showScreen()`, which hides the overlay.

22. **AirPlay mode on the player screen.** When Volumio's `service` is `airplay_emulation` (checked
    live against the Pi while it was on AirPlay: `service: "airplay_emulation"`, `trackType:
    "airplay"`, and title/artist/album all **empty strings**), `updateVolumioUI()` gets
    `airplay = true`: play/pause, prev and next go `LV_STATE_DISABLED` (dim outline,
    `set_btn_disabled_style()`; their callbacks also check `player_airplay` themselves) and the
    artist label reads "Airplay". Detected via `service`, deliberately not via the artist text —
    a local track by an artist actually called "Airplay" would otherwise disable the controls.
    Shuffle/repeat/volume are left as they were (not asked for).

23. **Restart / Shut down Volumio, from the menu.** Two action tiles in a row along the bottom of
    the full-screen menu (grey outline, so they don't read as destinations; the navigation tiles
    shrank from 84 to 56px tall to make room, two rows in `menu_grid` above them). They send
    Volumio's websocket `reboot` / `shutdown` events (`42["reboot"]` / `42["shutdown"]`, no
    payload) — read from the backend source (`volumio/Volumio2`,
    `app/plugins/user_interface/websocket/index.js`, which calls `commandRouter.reboot()` /
    `shutdown()`); Volumio's own docs don't list them, they're not on the REST API, and there's no
    event to restart only the service, so "restart" means rebooting the device. **Never test these
    against the real Pi** — a shutdown has to be undone by hand. Both go through a confirm dialog
    (`confirm_layer` inside `menu_list`, dimmed backdrop that also eats taps on the tiles behind
    it) with a one-line consequence, since neither can be undone from the clock. If the websocket
    isn't connected, `restartVolumio()`/`shutdownVolumio()` return false (`sendTXT`'s result) and
    the dialog says "Volumio is unreachable" instead of pretending it worked. Untested on hardware.
    **Follow-up: UI froze after pressing Restart.** Confirmed fixed on hardware. Cause (inferred from
    the code, never reproduced): while the Pi shuts down its port stays open for a few seconds, so `probeTask`
    still reported it reachable and `ws.loop()` started a blocking reconnect on the UI thread.
    Fix in `VolumioHandler.cpp`: `sendSystemAction()` sets a 30s reconnect hold-off
    (`SYSTEM_ACTION_HOLD_OFF_MS`) during which the probe idles and `loopVolumio()` doesn't call
    `ws.loop()` on a disconnected socket. Restart works on the real Pi with this.

24. **Artists screen stub.** New "Artists" entry in the dropdown menu, next to Library/Queue/
    Lights, going through the same `addScreen()` mechanism -- no new UI pattern needed. For now
    `VolumioArtists.cpp`'s `setupArtists()` just centers a "Coming soon" placeholder label; nothing
    is fetched from Volumio yet. Whatever browsing UI it gets later: confirmed live (see item 25)
    that the root `/api/v1/browse` listing already includes an `"uri": "artists://"` entry
    alongside `music-library`, so it's the same `/api/v1/browse` used by `VolumioLibrary.cpp`,
    just starting from that URI instead of `music-library` -- no dedicated "list artists" call
    needed.

25. **Library pagination: exact page count and total, from Volumio's own `count` field.** Checked
    live against `volumio.local` (a 117-item folder): passing `offset`/`limit` to `/api/v1/browse`
    (as Library already does, see item 11) makes the response's list include `"count"`, the
    folder's *real* total item count, stable across every offset tried -- not just how many items
    came back on that page. It's absent when browsing without `offset`/`limit` at all. This
    replaced `has_more`'s old "was the last page full?" guess (the item-11 false positive on an
    exact multiple of the page size) with `(fetch_page + 1) * LIB_PAGE_SIZE < total_count`, and
    the page indicator now reads "current/total" (e.g. "3/30") instead of just the current page
    number. Falls back to the old full-page guess, and to showing just the current page number,
    if a response is ever missing `count` (older Volumio version) -- `total_count` is `-1` in that
    case. `total_count` and the `count` JSON key are folder-scoped, re-fetched (and so
    re-validated) on every page turn, folder entry and Back, same as `item_count`.
    **Follow-up: Library sometimes got stuck on "Loading..." with Prev/Next/Up all disabled,**
    reported while paging quickly through "Music" (30 pages -- the only folder in this library big
    enough to invite rapid repeated Next taps; every other folder is a handful of items). Root
    cause, found reading the code rather than reproduced: `startFetch()`'s
    `xTaskCreatePinnedToCore()` call never checked its return value. Under the kind of heap
    pressure this project has hit before (see the heap quirks below), task creation can fail to
    get the 8KB stack it asked for; when it does, `fetching` was left `true` forever with no task
    running to ever set `fetch_done`, so `loopLibrary()` had nothing to process and the screen
    stayed on the "Loading..." `setStatus()` had already put it in, buttons and all, with no way
    out except a reboot. Fixed by checking the return value: on failure, `fetching` resets to
    `false` and the existing retryable-error UI (`showError()`, same one an HTTP failure uses)
    takes over instead. **The same unchecked-return pattern exists at the other three
    `xTaskCreatePinnedToCore()` call sites in the project** (`VolumioHandler.cpp`'s `probeTask`,
    `VolumioQueue.cpp`'s `queueTask`, `VolumioLibrary.cpp`'s own `folderActionTask`, and
    `TuyaLights.cpp`'s `fetchTask`) -- not fixed there yet, flagged here since it's the same class
    of bug and would show up as the same kind of "stuck forever" symptom in Queue/Lights/the
    folder context menu if heap pressure ever hits mid-operation there instead.

26. **The real cause of the Library freeze on "Music" (always page 3): LVGL's own memory pool,
    exhausted by its default theme's button-state transition, hanging forever inside an assert.**
    The `xTaskCreatePinnedToCore()` check above is real and worth keeping, but it wasn't what was
    actually happening -- serial logging (temporarily reinstated, see item 15) pinned the freeze
    to a specific point every single time: inside `updateNav()`'s `setEnabled(btn_next_page, ...)`
    call, always on the third page of "Music" (the only folder with enough pages to page through
    quickly). Read from LVGL's own source (`~/Arduino/libraries/lvgl/src/core/lv_obj_style.c` /
    `lv_obj.c`), confirmed exactly, not guessed: Prev/Next/Up flip `LV_STATE_DISABLED` on *every*
    page load (disabled during "Loading...", re-enabled once it's in) -- a real state change each
    time, not a no-op -- and the default theme gives that an 80ms fade transition
    (`LV_THEME_DEFAULT_TRANSITION_TIME`, `lv_conf.h`). Music's pages loaded in as little as 60ms,
    faster than the fade, so a new transition kept starting before the old one's cleanup ran,
    piling up `trans_t` nodes in LVGL's internal `style_trans_ll`. Those come from LVGL's *own*
    allocator (`lv_malloc()`), which with `LV_MEM_CUSTOM 0` (`lv_conf.h`, the default) is a
    `static` 48KB array totally separate from `ESP.getFreeHeap()` -- invisible to every heap check
    this project has ever added, which is why nothing looked wrong right up to the freeze. Once
    that pool was exhausted, `lv_malloc()` returned NULL, tripping `LV_USE_ASSERT_MALLOC`, whose
    handler (`LV_ASSERT_HANDLER`, `lv_conf.h`) is `while(1);` -- a silent, permanent hang, no
    crash, no reset, no watchdog (nothing was configured to catch it).
    - **First fix tried: `LV_MEM_CUSTOM 1`** (`lv_conf.h`) -- point LVGL at the general heap
      (`malloc`/`free`) instead of its own static pool, alongside moving `LvglHandler.cpp`'s
      display buffer from a `static` array to `malloc()` (the same fixed-size-static-array-on-
      this-chip lesson as item 17, needed because bumping `LV_MEM_SIZE` itself instead first hit
      the *static DRAM segment*'s own tight ceiling -- `dram0_0_seg overflowed`, same failure mode
      as item 17, at 128KB before the display buffer was even touched). **This fixed the Library
      hang but broke Tuya**: `TuyaLights.cpp`'s HTTPS calls need a large contiguous heap block for
      the TLS handshake (the same constraint item 11 already fought once), and LVGL's churn now
      fragmented that same general heap enough that Tuya's calls started failing with "connection
      refused" (`ensureToken()`/`fetchDeviceList()`, ultimately `tuyaRequest()`'s
      `http.errorToString()`) -- the Lights screen came up with no device buttons at all. Caught
      because the user noticed Lights had stopped working after this change went in; **reverted**.
    - **Real fix: leave `LV_MEM_CUSTOM 0` (LVGL's own isolated pool, so its churn can't starve
      Tuya's heap, as it always did before) and stop the actual waste** -- disable the transition
      on the three nav buttons directly (`addNavButton()`, `VolumioLibrary.cpp`), since an instant
      colour swap reads perfectly fine for a nav button and needed no transition in the first
      place. **First attempt at that, `lv_obj_set_style_transition(b, NULL, selector)`, was
      itself a crash**, also found by reading LVGL's source rather than guessing: `NULL` is a
      valid *value* for the property (`lv_obj_set_style_transition()`'s only job is storing
      whatever pointer it's given), but `obj_transition_states()` (`lv_obj.c`) treats "the
      property was found" as "safe to dereference `.ptr`" with no NULL check, so the very next
      real state change (the first folder entered, the first time Prev/Next/Up ever actually
      disables) dereferenced NULL and crashed -- on reboot the device naturally lands back on the
      default Player screen, which is exactly the symptom this looked like ("go back to
      playback" right after tapping the first folder) and is easy to mistake for a navigation bug
      rather than a crash+reboot. Fixed by giving it a real, valid, empty transition descriptor
      instead (`lv_style_transition_dsc_init()` with a zero-length `props` array) -- a legitimate
      "no properties transition" rather than a null pointer standing in for "no transition."
    - **Takeaways for next time**: (1) a memory pool can be completely invisible to every heap
      check in this codebase and still be the thing that runs out -- LVGL's own pool with
      `LV_MEM_CUSTOM 0` is exactly that, on top of the two heap-visibility quirks already below.
      (2) `LV_ASSERT_HANDLER`'s default `while(1);` turns any such exhaustion into a silent,
      un-diagnosable-without-serial hang; if a similar freeze ever recurs with no heap symptom,
      suspect this pool specifically before anything else. (3) Moving LVGL to the general heap is
      not a free fix on this project -- Tuya's TLS needs are heap-hungry enough that the two now
      compete if LVGL is allowed to share that heap; fixing LVGL's own waste is safer than giving
      it more room to leak into. (4) `lv_conf.h` is shared machine-wide (item 2) -- both the
      `LV_MEM_CUSTOM` flip and its revert happened there, so any future memory chase on an
      unrelated sketch on this machine should check this file's history too.

27. **Restart/Shut down moved into the scrollable menu grid.** They used to sit in their own fixed
    row along the bottom of the full-screen menu, always visible and never affected by scrolling
    (item 23). Per explicit request, they're now just the last two tiles in the same `menu_grid`
    every other entry lives in -- reaching them means scrolling past the rest of the menu first,
    like any tile that overflows the visible area. `addMenuAction()` (`UiHandler.cpp`) lost its
    absolute `x`/fixed-`y` positioning and now creates its tile inside `menu_grid` (flex-wrap
    placement, same as `addMenuEntry()`'s). The two calls that used to run from inside `setupUI()`
    (before any screen had registered its own tile) moved into a new exported
    `addSystemMenuActions()`, called once from `arduino32.ino`'s `setup()` **after** every
    `addScreen()`/`addHiddenScreen()` -- calling it any earlier would put these two first, not
    last, since `setupUI()` (which creates `menu_grid`) runs before any of the sketch's own
    `addScreen()` calls. `menu_grid`'s height also grew from a fixed two-row `138` to
    `SCREEN_HEIGHT - TOP_BAR_H` (fills the whole menu below the header) now that there's no
    separate fixed row below it to leave room for. Confirmed on hardware.

28. **The item 26 nav-button fix (empty transition descriptor) never actually worked, and the
    Library freeze recurred; the real fix and a shared `PaginationNav.cpp` came out of chasing
    it down again.** Reported after item 27's build was first flashed: same exact symptom as
    item 26 (stuck on "Loading..." on Music's third page) despite item 26's fix being untouched
    in the source. Reproduced once with temporary serial logging (`lv_mem_monitor()`, added and
    removed the same way item 15/25 did) and once without -- inconclusive on its own (it happened
    to not reproduce with the logging in place, which cost real time chasing a phantom "maybe it
    was transient" theory before it recurred again on a clean build) -- what actually settled it
    was reading `lv_theme_default.c` directly. The default theme attaches its OWN separate style
    objects to every `lv_button` (`transition_delayed` at the DEFAULT selector,
    `transition_normal` at `LV_STATE_PRESSED`, both transitioning `LV_STYLE_BG_COLOR` with a real
    ~80ms fade) when the button is created. `obj_transition_states()` (`lv_obj.c`) scans every
    style object attached to a widget independently and sums each one's own transition properties
    -- there is no "local style overrides theme style" rule for `LV_STYLE_TRANSITION` specifically.
    Item 26's fix only ever set an empty transition descriptor on *our own* local style at the
    same selectors, which never touched the theme's separate objects at all -- it happened to
    silence the `DISABLED`-state flip from page loads only because the theme's own `disabled`
    style has no transition of its own to compete with, so there was nothing else contributing
    there. Every rapid tap (press = enter `PRESSED`, release = leave it) kept independently piling
    up real transitions via the theme's `PRESSED`/`DEFAULT` objects the entire time, unaffected by
    item 26 -- which is exactly why this recurred under the same "paging quickly through Music"
    trigger. There's no public handle to the theme's own style objects to remove just those, so
    the real fix strips *every* theme-attached style from the button (`lv_obj_remove_style_all()`)
    instead of trying to override it, keeping only the corner radius (captured before removal, so
    the button doesn't visibly change) since every colour that matters is already set explicitly
    per state. Two follow-on mistakes while building this, both caught on hardware, not in review:
    (a) restoring only radius+bg_opa after the removal left the buttons looking visibly smaller,
    because the theme's default button style also carries a drop shadow that was extending their
    apparent footprint -- simplified away entirely (see (b)) rather than chased further; (b) a
    later ask to double the buttons' width appeared to do nothing, twice, because `lv_obj_set_size()`
    is itself backed by `LV_STYLE_WIDTH`/`HEIGHT` (`lv_obj_pos.c`) and was being called *before*
    `lv_obj_remove_style_all()` in the same function -- the removal was wiping out the size right
    after setting it. Fixed by moving the size call to after the removal, and simplified by
    dropping the shadow-restoration path entirely (a flat button reads fine here; matching the
    theme's exact drop shadow wasn't worth the extra lines it took to capture and restore).
    **Extracted into `PaginationNav.cpp`** (`createPagerButton()`, `setPagerEnabled()`) once
    settled, since `VolumioQueue.cpp` turned out to have its own separate, older copy of this same
    button -- *without* this fix, or item 25's earlier one -- that was equally vulnerable to the
    exact same freeze, just never hit because nobody had paged through a short-enough queue fast
    enough to trigger it. Both `VolumioLibrary.cpp` and `VolumioQueue.cpp` now call the shared
    version instead of each keeping their own copy in sync by hand.
    **Takeaway added to the four in item 26**: (5) LVGL's per-object style override (setting the
    same property locally) does not cancel a *theme-attached* style's own value for
    `LV_STYLE_TRANSITION` specifically -- `obj_transition_states()` sums every attached style
    object's transition props independently rather than doing highest-priority-wins for that one
    property, so neutralizing a theme's transition on a specific widget needs
    `lv_obj_remove_style_all()` (or removing the exact theme style object, if there's a handle to
    it), not a local override at the same selector.

- **LVGL's compiled-in fonts are ASCII-only** — the stock Montserrat fonts (`lv_conf.h`, shared,
  outside this repo) only cover code points 32-126 plus LVGL's own icon symbols (checked directly
  in the compiled font source, `lv_font_montserrat_18.c`'s `cmaps[]`). No curly quotes, en/em
  dashes, ellipsis, or accented Latin letters — all common in Volumio's metadata (title/artist/
  album, library/queue item names, from properly-tagged files) — which then rendered as a
  missing-glyph box on screen. First fixed with a text-rewriting workaround
  (`sanitizeText()`, rewriting smart punctuation to ASCII lookalikes), then replaced with the
  actual fix: `CustomFonts.h` declares 4 regenerated Montserrat fonts
  (`lv_font_montserrat_ext_{12,14,18,32}`, in `font_montserrat_ext_*.c`) with the glyph range
  extended to Latin-1 Supplement (accented letters) plus the specific smart-punctuation code
  points, built with `lv_font_conv` (`npx lv_font_conv`) from the exact same source LVGL's own
  bundled fonts use (`lvgl/scripts/generators/built_in_font/Montserrat-Medium.ttf` +
  `built_in_font_gen.py`'s icon-symbol list), so they're visually identical to the stock fonts,
  just with more glyphs. Adds roughly 250KB combined at the four sizes actually used — comfortably
  within the ~1.5MB of free flash. Every `&lv_font_montserrat_NN` reference in the code was
  swapped for `&lv_font_montserrat_ext_NN`, and every list/container showing external metadata
  (`list_library`, `list_queue`, the Library long-press context menu, `cont_lights`) got an
  explicit `lv_obj_set_style_text_font(..., &lv_font_montserrat_ext_14, 0)` so children inherit it
  instead of falling back to `LV_FONT_DEFAULT` (still the stock, ASCII-only montserrat_14).
- **Don't gate a UI value that needs to "tick" on its own periodic timer** if it's meant to be
  independent of another timer (e.g. the wall clock) — two separate `millis()`-gated timers that
  both reset to "now" each time they fire stay phase-locked forever regardless of being separate
  variables, and a fixed offset between them is still just a workaround, not a fix. Recompute
  from a real elapsed-time delta on every loop iteration instead (with a cheap no-op guard if the
  displayed value hasn't changed), like `UiHandler.cpp`'s `refreshPlaybackClock()` does. See
  item 9 above; the working reference for this pattern is
  `~/Documents/dev/led-pi/volumio_widget.py`'s `draw()`.
- **`/tinyart` has broken HTTP framing** (relevant only if album art is attempted again — not
  currently in the codebase, see item 8): no `Content-Length`, not chunked, `Connection:
  Keep-Alive` claimed but the socket is actually closed after the body. `HTTPClient` on ESP32
  can't handle this (silently reads 0 bytes); a raw `WiFiClient` reading until disconnect worked.
- **ESP32 heap capability mismatch** (also from the reverted album art work, but a general
  lesson): `ESP.getFreeHeap()`/`ESP.getMaxAllocHeap()` (and plain `malloc()`) don't necessarily
  reflect what's actually available for a generic byte buffer — some free RAM may only be usable
  for other capabilities (e.g. IRAM/execute). Use `heap_caps_malloc(size, MALLOC_CAP_8BIT)` +
  `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` for trustworthy numbers.
- **Reserving big buffers early (before `WiFi.begin()`) isn't automatically safe** just because
  it fixed one fragmentation problem (Library/Queue's arrays, and later `/tinyart`'s small
  buffers) — WiFi's own connection-time allocations compete for that same early, "pristine" heap.
  A later, bigger reservation (for HTTPS + JPEG decode buffers) starved it enough that WiFi never
  connected at all. Measure actual headroom before assuming "early = safe."
- **Colors look swapped on this display — root cause found, still deliberately not fixed.**
  `COLOR_ACCENT` is `0x1DB954` (green) but renders blue-violet. Read from the code: the flush
  (`LvglHandler.cpp`) calls `gfx->draw16bitBeRGBBitmap()`, which sends the buffer's bytes exactly
  as they sit in memory (`Arduino_TFT.cpp` → `_bus->writeBytes`), but LVGL 9's RGB565 buffer is
  little-endian (`lv_color16_t` bitfields, `LV_COLOR_16_SWAP` is a no-op in v9) while the ILI9341
  wants the high byte first — so **every pixel reaches the panel with its two bytes swapped**
  (black/white unaffected; `0x1DB954` → RGB565 `0x1DCA` → seen as `0xCA1D` ≈ violet; greys get
  tinted too). The one-line fix would be `draw16bitRGBBitmap()`, but the user is fine with the
  current look and the accent choice downstream assumes it, so it hasn't been touched — don't
  "fix" it without asking. Anything where the *actual* colour matters has to compensate instead:
  `DISPLAY_BYTE_SWAPPED` in `DisplayConfig.h` + `panelColor()` in `TuyaLights.cpp` pre-swap the
  colour palette's swatches. **If the flush is ever fixed, set `DISPLAY_BYTE_SWAPPED` to 0** or
  the palette goes wrong. This diagnosis is from reading the code, not from measuring the
  panel — if the palette swatches look off on the device, this is the first place to check.
- **Serial port**: the user runs Arduino IDE with its own Serial Monitor attached to
  `/dev/ttyUSB0` most of the time. Don't kill that process to grab the port without asking first
  — did this once already and it was disruptive. If you need serial output, either ask the user
  to paste what their monitor shows, or ask before taking the port.
- **`arduino-cli upload` fails with "port busy"** whenever Arduino IDE's monitor (or anything
  else) has the port open — same reasoning as above, ask first.
- **ESP32 core `4.0.0-alpha1`'s TLS is broken — every outbound HTTPS handshake fails**, not just
  to a specific host. Root-caused while building Tuya auth (item 10): DNS and raw TCP connect to
  port 443 both succeed, but the TLS handshake itself fails identically against Tuya's API *and*
  a control host (google.com), with an mbedtls error code (`0080:000D`) that the core's own
  `mbedtls_strerror()` can't even decode — this core is mid-transition to a new "tf-psa-crypto"
  backend, and `NetworkClientSecure`/`WiFiClientSecure` never calls the now-required
  `psa_crypto_init()` (confirmed: calling it explicitly first still didn't fix the handshake, so
  it runs deeper than that). Fixed by downgrading to the stable `esp32:esp32@3.3.11` core instead
  of chasing it further — don't reinstall the alpha core, and if a *different* HTTPS target ever
  fails, check `arduino-cli core list` before assuming it's a code bug.
- **`arduino-cli`'s real incremental-build cache is `~/.cache/arduino/sketches/<hash>/`, not the
  project's local `build/` directory.** Hit while adding the Artists screen: a compile failed at
  the *link* step with `undefined reference to setup()/loop()` and no compile error for any
  source file — deleting the local `build/` dir (`./arduino.sh clean`) and even the ESP32 core's
  own cache (`~/.cache/arduino/cores/...`) didn't fix it. `--verbose` showed why: `Using
  previously compiled file: .../sketch/arduino32.ino.cpp.o` — a stale/corrupt cached object for
  the sketch itself (not a library) was being relinked instead of rebuilt. Root cause not
  confirmed (never reproduced from a clean state), but the timing lines up with two
  `arduino-cli compile` calls having overlapped shortly before (one from this session backgrounded
  by a shell timeout, run right as another one started). Fixed by removing that sketch's whole
  cache dir under `~/.cache/arduino/sketches/` (find it via `arduino-cli compile --verbose` and
  look for `Using previously compiled file:` / `Using cached library dependencies for file:`
  lines, or just `rm -rf ~/.cache/arduino/sketches/*` if unsure which hash is this sketch's) and
  recompiling. If a compile ever fails at the link step with no source-level error, suspect this
  cache before the code.
