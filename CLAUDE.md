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
  the screen-timeout/backlight-off-after-30-seconds logic and wake-on-touch.
- [TouchHandler.cpp](TouchHandler.cpp) — XPT2046 touch reading, noise filtering.
- [UiHandler.cpp](UiHandler.cpp) — top bar (clock, dropdown menu / back button), the player screen
  (art, title/artist/album, transport buttons, volume), and `addScreen()`/`addHiddenScreen()`, the
  mechanism every other screen registers a full-screen page through. Since item 29, every screen
  is lazy-built and freed on hide: `addScreen(name, on_build, on_show, on_hide)` calls `on_build`
  the first time a screen is shown (construct its widgets into the given container), `on_show`
  every time (load/refresh its data), and `on_hide` when navigating away (tear the widgets back
  down, freeing them from LVGL's memory pool). The player and the full-screen menu overlay are the
  two exceptions, built once and kept alive for the app's lifetime -- see item 29 for why.
- [VolumioHandler.cpp](VolumioHandler.cpp) — the WebSocket connection to Volumio
  (`/socket.io/?EIO=3&transport=websocket`), playback control commands.
- [VolumioLibrary.cpp](VolumioLibrary.cpp) — music library browser (folders), HTTP polling via
  `/api/v1/browse`, paginated list UI, long-press context menu (play / queue / clear+play /
  update folder). Lazy-built/freed (item 29): `buildLibrary()`/`refreshLibrary()`/`hideLibrary()`
  are `addScreen()`'s on_build/on_show/on_hide -- the current folder/page are kept across a hide
  (small state, not LVGL objects) so reopening rebuilds the same view instead of resetting to the
  root.
- [VolumioQueue.cpp](VolumioQueue.cpp) — queue viewer/editor, HTTP via `/api/v1/getQueue` +
  `/api/v1/commands/?cmd=...`, per-item remove button. Lazy-built/freed (item 29):
  `buildQueue()`/`refreshQueue()`/`hideQueue()`. `getQueue` has no server-side paging, so the
  response is stream-parsed and only the current page's 4 rows are kept -- see item 34.
- [PaginationNav.cpp](PaginationNav.cpp) — `createPagerButton()`/`setPagerEnabled()`, the styled
  nav button (Prev/Next/Up/Clear) shared by Library and Queue's bottom bars -- see item 28.
- [VolumioArtists.cpp](VolumioArtists.cpp) — Artists screen (dropdown entry, via `addScreen`).
  Flat, paginated list of every artist, browsing `artists://` the same way `VolumioLibrary.cpp`
  browses `music-library` -- see item 30. Lazy-built/freed like every other screen (item 29) via
  `buildArtists()`/`refreshArtists()`/`hideArtists()`. Tapping an artist opens
  `VolumioArtistTracks.cpp`'s screen -- see item 31 for why that's tracks only, no album browsing.
  Long-press opens a context menu (Play / Add to queue / Clear and play) -- see item 32.
- [VolumioArtistTracks.cpp](VolumioArtistTracks.cpp) — per-artist track list (hidden screen, no
  dropdown entry, opened via `openArtistTracks()` from `VolumioArtists.cpp`). Paginated, tap a
  track to play it (addToQueue + play-Nth, same shape as `VolumioLibrary.cpp`'s `ACTION_PLAY`) --
  see item 31.
- [DisplayConfig.h](DisplayConfig.h) — pins, screen dims, shared constants (`COLOR_ACCENT`,
  `TOP_BAR_H`, `SCREEN_TIMEOUT_MS`).
- [CustomFonts.h](CustomFonts.h) + `font_montserrat_ext_{12,14,18,32}.c` — Montserrat fonts
  regenerated with a wider glyph range than LVGL's stock ones (accented letters, smart
  punctuation); see the quirks section below for why these exist and how to regenerate/resize
  them.
- [TuyaLights.cpp](TuyaLights.cpp) — "Lights" screen (dropdown entry, via `addScreen`).
  Authenticates against the Tuya Cloud API (`openapi.tuyaeu.com`, Simple mode signing), lists up
  to 5 devices (`/v2.0/cloud/thing/device?page_size=5`) with their live switch state fetched in a
  single batch call (`GET .../status?device_ids=...`, code `switch_led` -- item 29), and tapping
  one toggles it (`POST .../commands`); long-pressing one opens a hidden per-light brightness page
  (slider + colour palette + Back button). No unlink/remove action, by design. Both the main list
  and the brightness page are lazy-built/freed (item 29): `buildLights()`/`refreshTuyaLights()`/
  `hideLights()` and `buildBrightnessPage()`/`refreshBrightnessPage()`/`hideBrightnessPage()`.
  `tuyaRequest()`'s `WiFiClientSecure`/`HTTPClient` are kept alive across calls (item 29) instead
  of a fresh TLS handshake every time.

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
   refresh itself. **Signature grew to `addScreen(name, on_build, on_show, on_hide)` in item 29**
   once screens needed to build/free their widgets lazily instead of always existing from setup().
6. **Player screen redesign** to match a reference screenshot: album art placeholder (top-left)
   + title/artist/album to its right, elapsed/duration as plain text (no progress bar — never
   asked for), big round play/pause button centered with prev/next flanking it and
   shuffle/repeat further out, volume slider + numeric readout at the bottom. Mute button
   removed per request. Repeat was accidentally dropped once, then restored next to Next.
7. **Screen timeout**: backlight off after `SCREEN_TIMEOUT_MS` (30s, was 3 min, `DisplayConfig.h`) with no
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
    need to re-add both. It was added back a few times while chasing memory bugs (items 25, 28,
    29) and removed again each time: none is in the code now.
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

29. **A second, different freeze ("back to Player" after navigating Library) turned out to be a
    much bigger structural problem: every screen's full widget tree sat permanently in LVGL's
    48KB pool from boot, whether or not it was ever opened -- and Library/Tuya's real needs don't
    both fit in what's left over.** Reported right after item 27's menu-grid build was first
    flashed: same freeze symptom as item 26 (stuck on "Loading...", every touch dead), but this
    time triggered by switching from a folder a few levels deep in Library back to the Player
    screen, not by rapid Prev/Next taps. Diagnosed with `lv_mem_monitor()` checkpoints after each
    `setup()` step (temporarily reinstated per item 15's pattern): the pool was **already at
    54-88% used just from every screen's widgets existing**, before any navigation --
    `setupUI` (header/menu/dialog/player) 6180 bytes, Library 3128, Queue 3116, Artists 1092,
    Lights 7020 (almost all of it the brightness/colour sub-page's slider + 12 round swatches),
    Restart/Shutdown tiles 1216. A few folders into Library left as little as ~1.3KB free, and the
    Player screen's own render pass on switching to it needed more than that -- same
    `LV_ASSERT_HANDLER` hang as item 26, different trigger.
    - **`LV_MEM_SIZE` alone has no safe value**, confirmed by direct measurement, not guessed: 64KB
      fixed the Library/Player hang (biggest-free went from ~1-3KB to ~18-21KB in the same
      scenario) but broke Tuya Lights with "connection refused" -- this static pool and the
      general heap draw from the same physical SRAM (no PSRAM on this board), so the extra 16KB
      came directly off the heap's own largest-contiguous-block ceiling (~43KB -> ~27-33KB),
      undercutting the ~32KB a TLS handshake needs. A 56KB middle-ground value was tried and still
      failed intermittently -- not because the number was wrong, but because **the actual failure
      mode is heap fragmentation, not a fixed threshold**: two Lights refreshes back to back, with
      identical `heap_caps_get_largest_free_block()` readings at the start of each (34804 bytes,
      confirmed via `heap_caps_get_info()`'s `free_blocks`/`total_free_bytes`), one succeeded and
      the next failed with "connection refused" after Library navigation had run in between --
      the biggest-single-block number alone doesn't capture how fragmented the surrounding memory
      is. **If a memory number ever needs tuning again on this project, tune footprint, not this
      constant** -- reverted to the original 48KB.
    - **Two Tuya-side fixes landed first, before the real fix, and are worth keeping regardless**:
      (1) `tuyaRequest()`'s `WiFiClientSecure`/`HTTPClient` became `static` (kept alive across
      calls) instead of a fresh TLS handshake every call, same pattern `VolumioLibrary.cpp`'s
      `fetchTask()` already used for Volumio's own API. (2) `fetchDeviceList()` was doing 1 (list)
      + one-per-device (status) calls -- up to 6 HTTPS round trips per refresh, each leaving the
      heap a bit more fragmented than the last (measured: a 3-device refresh went from ~35KB
      biggest-free-block to under 2KB by its 4th call, *before Library was ever touched*).
      Switched to Tuya's own batch endpoint
      (`GET /v1.0/iot-03/devices/status?device_ids=id1,id2,...`, confirmed against
      [Tuya's docs](https://developer.tuya.com/en/docs/cloud/2faa3c9f3d), up to 20 IDs, same
      `status` code/value array shape as the per-device call) -- cuts every refresh to at most 2
      calls. Neither fix alone was sufficient (still fragmentation-sensitive under combined load),
      but both reduce how much churn there is to fragment things in the first place.
    - **The real fix: every screen is now lazy-built and freed on hide, not built once at
      `setup()` and kept forever.** `UiHandler.cpp`'s `addScreen()`/`addHiddenScreen()` gained
      `on_build`/`on_hide` alongside the existing `on_show` (signature in the file map above).
      `showScreen()` now tracks the currently-shown screen, fires its `on_hide` (expected to
      `lv_obj_clean()` its container and null out its own `lv_obj_t*` pointers) when switching
      away, and fires the target's `on_build` the first time it's shown after being torn down
      (including the very first time ever) before its `on_show`. Each module split its old
      `setupX()` into `setupX()` (just stores the container reference, no widgets, no network
      call), `buildX()` (the widget-creation code that used to run at boot), `refreshX()`/`showX()`
      (unchanged where it already existed as `on_show`; new for Library, which now re-fetches the
      *remembered* folder/page on a revisit instead of resetting to the root -- confirmed with the
      user that this, not resetting to a default, is the wanted behavior), and `hideX()` (new --
      `lv_obj_clean()` + null the widget pointers; **Queue's `hideQueue()` also frees the 19.2KB
      `titles[]` buffer**, the second-largest fixed cost in the project after Lights' brightness
      page, back to the general heap -- unless a fetch/op is still writing into it in the
      background, in which case it's deliberately left allocated rather than raced, and freed on
      the next hide that happens while idle instead). The brightness/colour sub-page (item 26's
      earlier "build once on first long-press, keep forever" interim fix) now goes through the
      same `on_build`/`on_hide` machinery instead of its own one-off `brightness_built` guard.
      Player and the full-screen menu overlay were deliberately left out of this -- the player is
      the app's default/home view and visited constantly, and the menu is opened/closed far more
      often than any content screen, so tearing either down on every hide would trade this
      problem for a worse one (constant rebuild churn); their combined 6180 bytes is cheap to keep
      relative to how often they're touched, unlike Library/Queue/Lights.
    - **Correctness risk found and fixed while building this**: a background fetch
      (`fetchTask`/`queueTask`/Tuya's task) can still be in flight when the user navigates away
      and the screen's widgets get torn down mid-flight. Each `loopX()` now checks its screen's
      own container/widget pointer is still non-NULL before touching it on a finished fetch;
      if it's NULL (torn down while in flight), the result is discarded (`fetching`/`busy` still
      reset to `false`) instead of touching freed/NULL `lv_obj_t*`s -- the next time the screen is
      shown, its `on_show` starts a fresh fetch anyway, so nothing is lost. `TuyaLights.cpp` has
      two separate guards for this (`cont_lights` for the main list, `label_bright_status` for the
      brightness page), since either one can be the screen that was mid-fetch when the user
      backed out via the menu instead of the page's own Back button.
    - **Measured result**: the same boot-time checkpoint that showed 54-88% pool usage before any
      navigation now shows nothing built until a screen is actually opened -- confirmed on
      hardware that the item 26/this item's freeze no longer reproduces even repeating the
      original trigger (several folders deep in Library, then switch to Lights) multiple times in
      a row, and Tuya Lights stayed reliable throughout the same test with `LV_MEM_SIZE` back at
      its original 48KB.

30. **Artists screen: flat, paginated list, on the item 29 lazy-load pattern from the start.**
    Replaced the "Coming soon" placeholder (item 24). Checked live against `volumio.local` (a
    119-artist library): `/api/v1/browse?uri=artists://` takes the identical `offset`/`limit`
    Library's `music-library` browsing already uses (item 11), and returns the identical `count`
    (the real total, item 25) once those are passed -- so `VolumioArtists.cpp` is essentially
    `VolumioLibrary.cpp`'s fetch/paginate/nav-button machinery (including the
    `xTaskCreatePinnedToCore` return-value check from item 25's follow-up) trimmed to a single
    flat level: no folder stack, no "Up" button, no long-press context menu, no track playback.
    Unlike its first implementation earlier in this project's history (built before item 29's
    lazy-load pattern existed, and reverted before ever reaching hardware), this one was written
    directly against the final pattern: `buildArtists()`/`refreshArtists()`/`hideArtists()` are
    `addScreen()`'s on_build/on_show/on_hide from day one, and `loopArtists()` has the same
    torn-down-widgets discard guard every other screen's loop function has.
    - **Drill-down into an artist is deliberately not implemented.** Checked live: browsing into
      `artists://<name>` (e.g. `artists://Arctic%20Monkeys`) returns **two separate lists** in
      `navigation.lists[]` -- "Albums (Artist)" and "Tracks (Artist)" -- each with its own
      independent `items`/`count`, rather than the single list every other browse response
      (folders, and the artist root itself) has. Passing `offset`/`limit` on that request appears
      to slice each list independently (an artist with 1 album/12 tracks and `limit=2` came back
      with the full 1-item Albums list untouched but only 2 of the 12 Tracks) rather than treating
      the two lists as one combined, offset-able sequence. Building a UI for that (two counters,
      two independent Prev/Next pairs, or some other layout) was deferred at the time -- **picked
      up in item 31**, which sidesteps the two-list problem entirely by only ever showing Tracks.

31. **Artist tracks: tapping an artist opens a paginated list of their songs (new
    `VolumioArtistTracks.cpp`), tap-to-play, no album browsing.** Item 30 confirmed browsing
    `artists://<name>` returns two independent lists (`Albums (<name>)`, `Tracks (<name>)`);
    rather than build UI for both, this only ever reads the one whose `title` starts with
    `"Tracks"` (matched by prefix, not array position, in case Volumio ever reorders them) and
    ignores Albums entirely -- confirmed live that `offset`/`limit` slices the Tracks list
    correctly on its own (`count` stays accurate across pages) regardless of what's in Albums.
    Each track item comes back with the same `service`/`uri`/`title` shape as a Library track
    (`uri` like `music-library/USB/.../song.flac`), so tapping one to play reuses the exact
    addToQueue + play-Nth pattern `VolumioLibrary.cpp`'s `ACTION_PLAY` already uses (queue length,
    `addToQueue`, `commands/?cmd=play&N=`), duplicated locally rather than exported since
    Library's version is `static`/private to that file.
    - **The artist's `uri` from the Artists list must NOT be re-encoded.** Volumio returns it
      already percent-encoded where it itself encodes things (confirmed live:
      `"uri": "artists://Arctic%20Monkeys"`, space as `%20`, colon/slashes left raw) -- passing it
      through the project's usual `urlEncode()` helper a second time turns `%20` into `%2520`
      (percent-encoding the literal `%` character) and breaks the request. Confirmed live that
      using the uri exactly as given, unencoded further, works correctly. If a similar uri ever
      needs re-fetching elsewhere, check whether it came from Volumio's own response (already
      encoded, use as-is) versus being hand-built (needs `urlEncode()`), rather than assuming one
      rule everywhere.
    - Built directly on the item 29 lazy-load pattern from the start (a new hidden screen, no
      dropdown entry, opened via `openArtistTracks(uri, name)`): `buildArtistTracks()`/
      `refreshArtistTracks()`/`hideArtistTracks()` are `addHiddenScreen()`'s on_build/on_show/
      on_hide, `loopArtistTracks()` has the same torn-down-widgets discard guard every other
      screen's loop function has, and `setupArtistTracks(artistsScreen, tracksScreen)` stores the
      Artists screen reference Back needs (`arduino32.ino` passes `addScreen("Artists", ...)`'s
      own return value straight into it). Unlike Library's "remember position across a hide,"
      `openArtistTracks()` always resets to page 0 -- opening this screen is always a fresh
      navigation to whichever artist was just tapped (possibly a different one each time), not a
      return to a paused state.
    - `MAX_SCREENS` in `UiHandler.cpp` bumped 6 -> 8 for headroom (this screen brought the count
      already in use to 6, its exact old ceiling).
    - Confirmed on hardware: tapping a track starts playback and Back returns to the Artists list.

32. **Long-press an artist for the same Play / Add to queue / Clear and play menu Library's
    folders have** (item 3), minus "Update folder" -- an artist here is a virtual grouping across
    however many real folders, not something Volumio can rescan as a single unit the way a folder
    can. Confirmed live against `volumio.local` before implementing: `addToQueue` with an artist's
    own `uri` (`artists://<name>`, exactly as given by the `artists://` list, not re-encoded --
    same caution as item 31) queues every one of that artist's tracks, the same way a folder's uri
    does for `VolumioLibrary.cpp` -- queue length went from 81 to 93 for a 12-track artist in the
    live test. `VolumioArtists.cpp` gained the same `PRESSED`/`LONG_PRESSED`/`CLICKED` +
    `long_press_handled` row-event pattern Library's `item_cb()` already uses (a tap still opens
    the track list, item 31, unless the press was already consumed as a long-press), and a
    duplicate (not exported/shared) copy of Library's `postJson()`/`queueLength()`/action-task
    machinery, since Library's is `static` to that file. One difference from Library's version:
    the request body's `"service"` is hardcoded to `"mpd"` rather than read from the item, since
    artist entries from `artists://` don't carry their own `service` field the way folder/track
    items do.

33. **Lights lost their initial on/off state, and the brightness slider stopped working -- both
    from item 29's batch status call.** Checked live: `/v1.0/iot-03/devices/status?device_ids=...`
    works and has the expected shape, but it's the only Tuya response big enough (~2.6KB) to come
    back `Transfer-Encoding: chunked` (list/per-device/command responses all carry a
    `Content-Length`). `tuyaRequest()` fed `http.getStream()` straight into `deserializeJson()`,
    which then saw the raw chunk-size lines and failed; `fetchDeviceList()` ignores a failed status
    lookup on purpose, so every light silently stayed at its defaults (off, white mode, 100%). The
    slider then sent `bright_value_v2` to a bulb actually in colour mode, which changes nothing
    visible. Fixed by reading the body with `http.getString()` (which decodes chunked) before
    parsing. Confirmed on hardware. Any Tuya response that grows past a couple of KB can come back
    chunked, so never parse `getStream()` directly here.

34. **Queue didn't load for long queues: `getQueue` has no server-side pagination.** Checked live
    (330-item queue): `/api/v1/getQueue` ignores `offset`/`limit`, `start`/`count` and `page`
    and always returns the whole queue -- 233KB, every item carrying uri/albumart/samplerate etc.
    `fetchQueue()` deserialized all of it into one `JsonDocument` (filtered, but still 330 entries)
    into a 200-slot, 19.2KB `titles[]` heap buffer, which doesn't fit on this heap. Now the
    response is streamed element by element (find `"queue"` + `[`, `deserializeJson()` one object
    at a time with a filter, then read the `,`/`]` separator by hand), counting every item but
    keeping only the requested page's 4 titles in a small static array. That means one fetch per
    page turn, the same model as Library (item 11), at the cost of re-downloading the full body
    each time. If the queue shrank below the requested page, the same task re-fetches the new
    last page. Removing a row still drops it locally right away, then reloads the page after a
    400ms delay (`OP_REFRESH_AFTER_REMOVE`; the removal goes over the WebSocket, so the delay is a
    guess at how long Volumio takes to apply it) to pull the next item up. Also checks
    `xTaskCreatePinnedToCore()`'s return now (item 25's follow-up). **Untested on hardware** -- if
    page turns feel slow, the byte-by-byte stream read of the 233KB body is the first suspect.

35. **"Update DB" tile in the menu**, placed before Restart/Shut down (`addSystemMenuActions()`).
    It sends Volumio's websocket `42["updateDb"]` with no payload (`updateLibraryDb()` in
    `VolumioHandler.cpp`). With no URI, Volumio updates the whole library. The folder context menu
    sends the same event with a URI to update just that folder. It reuses the Restart/Shut down
    confirm dialog, so if the websocket is down the dialog says "Volumio is unreachable". Unlike
    those two, it doesn't set the reconnect hold-off. Confirmed on hardware.

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
