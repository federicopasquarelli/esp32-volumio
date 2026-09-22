# Debugging: album art not showing

Status: **resolved for the PNG case.** Fetch, allocation and decode all confirmed reliable on
the physical device (7/7 correct outcomes across a fresh batch of real tracks — 6 real PNGs
loaded, 1 correctly fell back to the placeholder because Volumio genuinely has no art for it — 0
allocation failures). Bug #3 (some albums' art is JPEG, not PNG) remains an open, accepted
coverage gap, not a bug — see its section below.

## Goal

Show real album art on the player screen (in [UiHandler.cpp](UiHandler.cpp)'s art slot, currently
a colored placeholder box), fetched from Volumio, falling back to the placeholder when there's no
real art.

## Where the code lives

[VolumioArt.h](VolumioArt.h) / [VolumioArt.cpp](VolumioArt.cpp). Public API:
- `setupAlbumArt(parent, x, y, w, h)` — creates the placeholder box + an `lv_image` (hidden until
  real art loads), called once from `UiHandler.cpp`.
- `requestAlbumArt(artist, album)` — called from `updateVolumioUI()` on every `pushState`; no-ops
  if artist+album match the last request, so it only actually fetches on track change.
- `loopAlbumArt()` — called from `arduino32.ino`'s `loop()`, applies a finished background fetch
  to the widgets.

## Why `/tinyart` instead of the `albumart` field from pushState

Tried the obvious thing first: Volumio's `pushState`/`getState` includes an `albumart` field
(e.g. `/albumart?cacheid=...&path=...`). Fetched it directly with curl against the real device
(`volumio.local:3000`) for a track from the local USB library:

```
$ curl -s -D- -o art.bin "http://volumio.local:3000/albumart?cacheid=394&path=..."
Content-Type: image/jpeg
Content-Length: 267856
$ file art.bin
art.bin: JPEG image data, ..., progressive, precision 8, 1000x1000, components 3
```

**Progressive JPEG, 1000x1000.** Confirmed via web search that TJpg_Decoder (the realistic
lightweight JPEG option for ESP32) explicitly does not support progressive JPEG — "would require
much more memory" — and 1000x1000 is far too large regardless. This is a hard blocker for local
library tracks; user was asked and explicitly chose *not* to go down the "decode+resize
client-side" route for this reason (see conversation — local files always ended up progressive
JPEG in testing, so that approach mainly benefits streaming-sourced art, e.g. Spotify).

Found Volumio also serves `/tinyart/<artist_name>/<album_name>/<size>` (per Volumio's REST API
docs — size is one of `small`(34)/`medium`(64)/`large`(174)/`extralarge`(300), spaces in
artist/album become underscores). Verified with curl:

```
$ curl -s -D- -o t.bin "http://volumio.local:3000/tinyart/The_Black_Dahlia_Murder/Abysmal/medium"
$ file t.bin
t.bin: PNG image data, 64 x 64, 8-bit/color RGBA, non-interlaced
```

A real, small, non-progressive PNG — decodable. **This is what `VolumioArt.cpp` uses.** Also
verified the "not found" case (bogus artist/album) returns Volumio's own placeholder, which is
always a baseline JPEG at 240x240 — since we only ever try to decode PNG, that difference doubles
as our "is this real art" signal, no separate metadata check needed.

Requested size is `medium` (64x64), matched 1:1 to the art box size in `UiHandler.cpp` so no
client-side scaling is needed.

## LVGL PNG decoder

`~/Arduino/libraries/lv_conf.h` (global file, **outside this repo**, shared by every sketch on
this machine) had `LV_USE_PNG 0` — but that's an LVGL 8 flag that no longer does anything on
LVGL 9; the real flag is `LV_USE_LODEPNG`, also 0. Set `LV_USE_LODEPNG 1`. Confirmed via LVGL's
own bundled example (`examples/libs/lodepng/img_wink_png.c`) the exact usage pattern for loading
a PNG from a RAM buffer:

```cpp
lv_image_dsc_t dsc;
dsc.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;  // or RAW; either signals "not raw pixels, decode me"
dsc.header.w = 64;
dsc.header.h = 64;
dsc.data = png_bytes;
dsc.data_size = png_byte_count;
lv_image_set_src(img_obj, &dsc);
```
`lodepng`'s registered decoder ignores the `header.cf`/`w`/`h` I set anyway — it reads the PNG's
own signature/IHDR directly from `data` to get real dimensions — but setting them matches the
documented example and costs nothing.

## Bug #1 (found + fixed): HTTPClient reads 0 bytes

First working version used `HTTPClient`. First device test:
```
[Art] fetched 0 bytes, not usable
```
`/tinyart`'s response has **no `Content-Length` and isn't chunked**, despite claiming
`Connection: Keep-Alive`. Verified with `curl -v` and Python's `http.client` from this machine —
both work by reading until the socket actually closes (which it does, despite the Keep-Alive
claim — seemingly a Volumio/Express quirk specific to this route). `HTTPClient` on ESP32 needs
one of those framing mechanisms and apparently just gives up silently otherwise.

**Fix applied**: bypass `HTTPClient` for this request. `fetchTask()` now opens a raw `WiFiClient`,
writes the GET request itself (`Connection: close`), reads/discards the status line + headers
manually, then reads the body in a loop until `!client.connected() && !client.available()`, with
an 8s idle timeout and a byte cap as a safety net.

Verified this got further — next test showed real progress:
```
[Art] status='HTTP/1.1 200 OK' ok=1
[Art] skipped 4 header lines, connected=1 available=5631
```
So the raw-socket approach does work — request goes out, headers parse, body bytes (5631 of them,
matching a real PNG) sit ready to read.

## Bug #2 (found, fix applied but **not yet verified**): allocation failing

Body read never happens because the buffer allocation fails:
```
[Art] malloc(20480) FAILED, free heap=72148 largest block=45044
```
(Cap was originally 32768, lowered to 20480 first — same failure either way.) The numbers don't
add up at face value: 45044 reported as the largest free block, but a 20480 allocation still
failed. Working theory: `ESP.getMaxAllocHeap()`/plain `malloc()` don't operate on the same RAM
capability — some of that "free" memory may only be usable for other purposes (e.g.
execute/IRAM-capable regions), not a generic byte buffer.

**Fix applied, NOT YET TESTED**: switched to `heap_caps_malloc(ART_MAX_BYTES, MALLOC_CAP_8BIT)`
and log `heap_caps_get_free_size(MALLOC_CAP_8BIT)` /
`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` instead — capability-correct numbers, should
either succeed or give trustworthy figures to reason about next. This is the current state of
`VolumioArt.cpp` — compiles, but the session ended before the user could flash and report back.

## Bug #2, confirmed: real (not reporting-artifact) heap fragmentation

Switched `/tinyart` size from `medium` (64x64) to `small` (34x34, `ART_MAX_BYTES` 8192), scaled up
to 64x64 in the UI via `lv_image_set_scale()` (source dims hardcoded as `ART_SRC_PX`, since
lodepng's decoder ignores the `header.w/h` I set anyway and ATM there's no need to read them back).

Flashed and sampled several track changes live (forcing `replaceAndPlay` on known local-library
folders via curl while tailing serial). Results were genuinely mixed:

```
Abysmal:    (heap dropped ~8KB and stayed down across heartbeats — inferred success, log missed)
Deflorate:  alloc(8192,8BIT) FAILED, free=19624 largest=6900
Everblack:  alloc(8192,8BIT) ok, fetched 3088 bytes, PNG          <- confirmed real success
Nocturnal:  alloc(8192,8BIT) FAILED, free=19852 largest=8180
Ritual:     status='' — request itself glitched (rapid-fire track changes, see caveat below)
```

**The earlier "numbers don't add up" confusion was just print ordering, not a real
inconsistency**: `heap_caps_malloc()` runs *before* the `Serial.printf()` that logs
free/largest, so on success those numbers are the *post*-allocation remainder (naturally smaller
than what was asked for), and on failure they're the true pre-attempt state (and in every logged
failure, `largest` genuinely was < requested size, e.g. `largest=8180` for `FAILED` on an 8192
request — consistent, not contradictory). So: this is real, page-to-page fragmentation on a
board with no PSRAM, not a measurement bug. Success rate sampled so far: roughly 1 in 3-4 track
changes actually gets a large enough contiguous block for even the 8KB "small" request.

**Confirmed working when it succeeds**: "Everblack" fetched a real PNG (3088 bytes) — the full
pipeline (raw-socket fetch → PNG magic check → `heap_caps_malloc` → `lv_image_set_src` with
`LV_COLOR_FORMAT_RAW_ALPHA` → `lv_image_set_scale`) runs without error. Have **not** visually
confirmed on the physical display that it renders correctly (no camera access) — worth a glance
next time someone's near the device, ideally right after a track change to something in the
Black Dahlia Murder discography (reliable PNG source, see below).

**Minor separate wrinkle**: "Ritual" got `status=''` — the raw-socket status line came back empty,
distinct from the normal "200 OK" or connect-failure paths. Only seen once, while firing track
changes in rapid succession (~1s apart) well faster than any real user would skip tracks. Probably
a transient overlap between the previous fetch's socket teardown and the new one's connect;
not chased further since it's not representative of real usage.

## Bug #3, discovered (not a regression — a coverage limit): art format varies per album

Volumio's `/tinyart` doesn't consistently return PNG for real (found) art — it depends on
whatever format its thumbnailer (sharp) picked for that source file. Confirmed via curl:

```
$ curl -s -D- -o t.bin ".../tinyart/Buena_Vista_Social_Club/Buena_Vista_Social_Club_(25th_Anniversary_Edition)/small"
$ file t.bin
t.bin: JPEG image data, ..., progressive, precision 8, 34x34, components 3
```

A genuine 34x34 **progressive JPEG** for real, found art — not Volumio's "not found" placeholder.
Since this codebase deliberately only decodes PNG (progressive JPEG is undecodable by any
realistic lightweight ESP32 decoder regardless of size, see the `/albumart` investigation above),
this specific album's art will *never* show, correctly falling back to the placeholder — this
isn't a bug in the fetch/alloc code, it's an inherent gap in the "PNG-only" approach chosen
earlier (and re-confirmed as the right call at the time, given the alternative — decoding
JPEG too — hits the same progressive-JPEG wall for some fraction of albums anyway).

No fix attempted for this one — it's a real product decision (accept the coverage gap, or take on
a JPEG decoder for the baseline-JPEG subset, which still won't cover progressive JPEGs like this
one) rather than a bug to silently patch.

## Bug #2 fix: reserve the buffers once, at boot, instead of per-fetch

The per-fetch `heap_caps_malloc()` (even at a modest 8KB) was only succeeding ~25-30% of the
time once WiFi/Library/Queue had fragmented the heap — real fragmentation, not a measurement
artifact (see the historical numbers below for how that was confirmed). Root fix: stop asking the
heap for memory per-track-change at all.

`initAlbumArtMemory()` (new, in `VolumioArt.cpp`) calls `heap_caps_malloc(ART_MAX_BYTES,
MALLOC_CAP_8BIT)` **twice, once, at the very start of `setup()`** — called from `arduino32.ino`
right after `setupLibrary()`/`setupQueue()`, before `WiFi.begin()` — same reasoning as why those
two already allocate their big arrays early. Both buffers (`art_dl_buf` — download scratch,
`art_active_buf` — what's actually displayed, copied from the scratch buffer once a download
validates as PNG) are kept forever, never freed. `requestAlbumArt()`/`fetchTask()`/
`loopAlbumArt()` all just reuse them; there is no more per-track-change allocation.

Tried plain `static` C arrays first instead of `heap_caps_malloc()` — **that doesn't work**: two
8KB static arrays overflowed the linker's `dram0_0_seg` (a separate, much smaller, statically-
sized region than the general runtime heap) by 9728 bytes. That segment was already nearly full
from LVGL/WiFi/lwIP's own static buffers. Heap allocation (once, early) is the right tool; a
compile-time array is not.

**Verified live on-device** after the fix, cycling through 7 real tracks via forced
`replaceAndPlay`: Abysmal, Everblack, Deflorate, Nocturnal, Ritual and Unhallowed all logged
`[Art] fetched N bytes, PNG` and loaded correctly (0 allocation failures across the whole batch,
where before the fix roughly 2 out of every 3 attempts failed to allocate). Miasma correctly
fell back to the placeholder — confirmed via curl that Volumio's `/tinyart` for that one album
really is its "not found" placeholder (240x240 baseline JPEG, 11220 bytes), not a fetch/alloc
problem.

Have not visually confirmed the rendered image on the physical screen (no camera access) — the
full pipeline runs without error and the display object is unhidden, but worth a glance next
time someone's near the device.

## Bug #4 (found + fixed): special characters in artist/album names were being over-escaped

Found while double-checking against the user's real, live queue (a 330-track King Gizzard & The
Lizard Wizard queue) rather than the single-album test folders used above. `/tinyart` genuinely
has no art for most of it — but investigating why turned up a real, separate bug: Volumio's
`/tinyart/<artist>/<album>/<size>` route does **not** decode percent-escapes in the artist/album
path segments. Confirmed directly with curl:

```
.../tinyart/King_Gizzard_%26_The_Lizard_Wizard/Chunky_Shrapnel/small  -> placeholder (no match)
.../tinyart/King_Gizzard_&_The_Lizard_Wizard/Chunky_Shrapnel/small    -> real PNG (3281 bytes)
```

Same pattern confirmed with an apostrophe (`Fool_%27em_All` fails, `Fool_'em_All` succeeds — that
particular album turned out to be JPEG-sourced anyway, Bug #3, but the *lookup* only succeeded
with the literal character). `urlPathEncode()` in `VolumioArt.cpp` was percent-escaping any
non-alphanumeric character, which is "correct" URL-encoding in general but was actively breaking
lookups for any real-world artist/album containing `&`, `'`, or similar — likely a meaningful
chunk of any real library.

**Fix**: only escape what would actually corrupt the raw HTTP request line we build by hand (the
`/` route separator, `?`/`#` delimiters, `%` itself, and control bytes) plus space→underscore per
Volumio's documented convention; everything else, including punctuation, now goes through as the
raw UTF-8 byte. Verified live: the device's own fetch for the byte-for-byte matched a direct curl
request using the literal `&` (1130 bytes, both — that particular current album ("Phantom Island",
a brand new release) turned out to be a Bug #3 JPEG case, but the *lookup itself* is now
confirmed correct).

## Bug #3: art format varies per album (open, accepted gap, not further pursued)

Volumio's `/tinyart` doesn't consistently return PNG for real (found) art — depends on whatever
format its thumbnailer (sharp) picked for that source file. Confirmed via curl for one real
album (Buena Vista Social Club) that it can return a genuine 34x34 **progressive JPEG** for real
art, not just for the "not found" placeholder. Since this codebase deliberately only decodes PNG
(progressive JPEG is undecodable by any realistic lightweight ESP32 decoder regardless of size —
see the `/albumart` investigation above), that album's art will never show. This is a real,
accepted product trade-off, not something further fixed here — revisit only if it becomes
important enough to justify adding a baseline-JPEG decoder (which still wouldn't cover the
progressive case).

## Historical notes: what the earlier (now superseded) numbers meant

Kept for context, in case fragmentation-related issues resurface elsewhere:

- **Print-ordering confusion, resolved**: `heap_caps_malloc()` ran *before* the diagnostic
  `Serial.printf()` logging free/largest, so on success those numbers were the *leftover* space
  after taking the memory (naturally smaller than what was asked for) — not a contradiction, just
  easy to misread. On failure they were the true pre-attempt state, and in every logged failure
  `largest` genuinely was less than the requested size.
- **Real fragmentation, not a capability-reporting bug**: plain `malloc()` / `ESP.getFreeHeap()`
  can report misleading numbers because they span RAM capabilities a byte buffer can't actually
  use; `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` gives the trustworthy figure. Both
  were compared side by side during this investigation and, once capability-matched, the numbers
  were internally consistent — the fragmentation was real, this board (no PSRAM) just doesn't
  reliably have 8KB+ contiguous free once WiFi/Library/Queue have all initialized.

## Environment notes for whoever picks this up

- Real device reachable during this debugging session at `volumio.local:3000`, connected via USB
  at `/dev/ttyUSB0`.
- **The user's own Arduino IDE Serial Monitor is usually attached to that port.** Don't kill it to
  grab the port yourself without asking — ask the user to paste what they see, or ask before
  taking it over. (Happened once already this session, was disruptive, board also
  disconnected/reconnected around the same time — possibly related, never fully confirmed.)
- Test artist/album used throughout: "The Black Dahlia Murder" / "Abysmal" (local USB library).
