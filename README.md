# ESP32 Volumio Remote & Clock (CYD)

A touchscreen remote control and desk clock for [Volumio](https://volumio.com/), running on the
**ESP32-2432S028R "Cheap Yellow Display" (CYD)**: an ESP32 with a 2.8" ILI9341 320x240 TFT and an
XPT2046 resistive touch panel. Built with Arduino, LVGL 9, WebSockets and ArduinoJson.

It talks to Volumio over its Socket.IO WebSocket (live player state) and its REST API (library
browsing, queue management), so it should work with any Volumio install on your network
(Raspberry Pi or otherwise).

## Features

- **Now playing**: title, artist and album (accented characters and smart punctuation supported),
  elapsed/total time, play/pause, previous/next, shuffle, repeat, and a volume slider.
- **Clock**: NTP-synced time in the header, with your timezone.
- **Library browser**: browse Volumio's music library folder by folder, paginated, with page
  count. Long-press a folder to *Play*, *Add to queue*, *Clear and play*, or *Update folder*.
- **Artists**: paginated list of every artist. Tap an artist to see their tracks, and long-press
  one to play or queue everything by them.
- **Queue**: view the current queue, tap a track to jump to it, remove tracks.
- **System actions** (with confirmation): update the library database, restart or shut down the
  Volumio device.
- **AirPlay aware**: transport controls are disabled while Volumio is playing via AirPlay.
- **Offline notice**: the UI stays responsive and shows "Volumio is unreachable" when the server
  is down, and reconnects automatically.
- **Screen timeout**: the backlight turns off after 30 seconds without a touch. The first touch
  only wakes the screen and returns to the player.

## Hardware

- **ESP32-2432S028R** ("Cheap Yellow Display"), ESP32-WROOM-32, no PSRAM.
- A USB cable (the board's USB-serial chip is a CH340, so you may need its driver on Windows/macOS).
- A Volumio server on the same network.

Pins are set in [DisplayConfig.h](DisplayConfig.h) for the common CYD layout:

| Function | Pins |
|---|---|
| Display (ILI9341, HSPI) | SCK 14, MOSI 13, MISO 12, CS 15, DC 2 |
| Backlight | 21 (some board revisions use 27) |
| Touch (XPT2046, VSPI) | SCK 25, MOSI 32, MISO 39, CS 33 |

If touches land in the wrong place, adjust the calibration ranges in
[TouchHandler.cpp](TouchHandler.cpp).

## Software requirements

- [Arduino IDE](https://www.arduino.cc/en/software) 2.x or [arduino-cli](https://arduino.github.io/arduino-cli/).
- **ESP32 board package `esp32:esp32` version 3.3.x** (tested with 3.3.11). Avoid the 4.0.0
  alpha releases, which have a broken TLS stack.

### Libraries

Install these from the Arduino Library Manager (tested versions in parentheses):

| Library | Author | Tested |
|---|---|---|
| lvgl | kisvegabor | 9.6.0 |
| GFX Library for Arduino | Moon On Our Nation | 1.6.8 |
| XPT2046_Touchscreen | Paul Stoffregen | 1.4 |
| WebSockets | Markus Sattler | 2.7.2 |
| ArduinoJson | Benoit Blanchon | 7.4.3 |

### LVGL configuration (`lv_conf.h`)

LVGL needs an `lv_conf.h` placed **next to the `lvgl` folder** in your Arduino libraries
directory (e.g. `~/Arduino/libraries/lv_conf.h`, not inside `lvgl/`):

1. Copy `lvgl/lv_conf_template.h` to `libraries/lv_conf.h`.
2. Change the `#if 0` at the top to `#if 1` to enable it.
3. Set `#define LV_MEM_SIZE (48U * 1024U)`. Don't go higher: LVGL's memory pool shares RAM with
   WiFi and the network buffers, and a bigger pool causes HTTP failures on this board.

Leave the other defaults as they are (16-bit colour depth, Montserrat 14 as the default font).

## Setup

1. Clone the repository. The folder must be named `esp32-volumio` to match the sketch:
   ```bash
   git clone <repo-url> esp32-volumio
   cd esp32-volumio
   ```
2. Create your secrets file from the template:
   ```bash
   cp arduino_secrets.h.template arduino_secrets.h
   ```
3. Edit `arduino_secrets.h`:
   ```cpp
   #define SECRET_SSID "YOUR_WIFI_NAME"
   #define SECRET_PASS "YOUR_WIFI_PASSWORD"
   #define SECRET_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"  // POSIX TZ string
   #define SECRET_VOLUMIO_HOST "volumio.local"            // hostname or IP
   #define SECRET_VOLUMIO_PORT 3000
   ```
   `SECRET_TIMEZONE` is a POSIX TZ string. Look yours up in
   [this list](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv). If
   `volumio.local` doesn't resolve on your network, use the Volumio device's IP address.
   `arduino_secrets.h` is gitignored, so your credentials stay out of the repository.

## Build and flash

The sketch doesn't fit the default partition scheme. **Select the `Huge APP` partition scheme**
or the upload fails with a "sketch too big" error.

### Arduino IDE

1. Open `esp32-volumio.ino`.
2. **Tools → Board**: "ESP32 Dev Module".
3. **Tools → Partition Scheme**: "Huge APP (3MB No OTA/1MB SPIFFS)".
4. Select the board's port and click **Upload**.

### arduino-cli

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .
arduino-cli upload  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app -p /dev/ttyUSB0 .
```

Or use the helper script, which has the board settings built in (edit `PORT` at the top if your
board isn't `/dev/ttyUSB0`):

```bash
./arduino.sh compile   # build
./arduino.sh flash     # build and upload
./arduino.sh clean     # clean rebuild
```

## Usage

- Tap the menu button (top right) to switch between **Player**, **Library**, **Queue** and
  **Artists**, or to reach **Update DB**, **Restart** and **Shut down**.
- Tap an item to open or play it. Long-press a folder or artist for queue options.
- Use the Prev/Next buttons at the bottom of a list to page through it.

## Troubleshooting

- **Screen stays blank or frozen after boot**: the device waits for WiFi before starting. Check
  your SSID and password (the ESP32 only supports 2.4 GHz networks).
- **"Volumio is unreachable"**: check `SECRET_VOLUMIO_HOST`/`SECRET_VOLUMIO_PORT`, and try the
  IP address instead of `volumio.local`.
- **Colours look different from the hex values in the code** (the green accent shows as
  blue-violet): known, the display flush sends RGB565 bytes swapped. Switching `LvglHandler.cpp`'s
  flush to `draw16bitRGBBitmap()` shows the colours as written.
- **Upload fails with "port busy"**: close any serial monitor that has the port open.
- **Link error with no source error** (e.g. `undefined reference to setup()`): clear arduino-cli's
  build cache (`~/.cache/arduino/sketches/`) and compile again.

## Project structure

| File | Purpose |
|---|---|
| `esp32-volumio.ino` | `setup()`/`loop()`, wires the modules together |
| `LvglHandler.*` | LVGL init, display flush, touch input, screen timeout |
| `TouchHandler.*` | XPT2046 reading and noise filtering |
| `UiHandler.*` | Header, menu, player screen, screen registration |
| `VolumioHandler.*` | WebSocket connection and playback commands |
| `VolumioApi.*` | Shared Volumio REST helpers |
| `VolumioLibrary.*` | Library browser |
| `VolumioArtists.*`, `VolumioArtistTracks.*` | Artist list and per-artist tracks |
| `VolumioQueue.*` | Queue viewer/editor |
| `QueueActionMenu.*` | Long-press Play / Add to queue / Clear and play menu |
| `PaginationNav.*` | Shared Prev/Next buttons |
| `CustomFonts.h`, `src/fonts/` | Montserrat fonts with accented characters and smart punctuation |
| `DisplayConfig.h` | Pins, screen size, colours, timeout |

Keywords: ESP32, ESP32-2432S028, Cheap Yellow Display, CYD, Volumio, remote control, music player,
touchscreen, LVGL, ILI9341, XPT2046, Arduino, Raspberry Pi, smart clock.
