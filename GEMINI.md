# [SYSTEM_HUB]: CYD_VOLUMIO_SMART_CLOCK

## [HARDWARE_IDENT]
- **BOARD**: ESP32-2432S028 (Cheap Yellow Display v2/v3)
- **DISPLAY**: ILI9341 2.8" SPI (320x240)
  - **BUS**: Shared SPI (HSPI)
  - **TFT_PINS**: SCK:14, MOSI:13, MISO:12, CS:15, DC:2, RST:-1
  - **BACKLIGHT**: Pin:21 (Primary) / 27 (Alt). State: HIGH=ON.
- **TOUCH**: XPT2046 Resistive
  - **TOUCH_PINS**: CS:33, IRQ:-1 (Polling Mode)
  - **MAPPING**: x:[200,3700]->[0,320], y:[240,3800]->[0,240]
  - **NOISE_FILTER**: Ignore `p.x/y == 8191` (Bus Contention). Min pressure `p.z > 400`.

## [CREDENTIAL_MANAGEMENT]
- **SECRETS**: WiFi and Volumio connection settings are stored in `arduino_secrets.h`.
- **SECURITY**: `arduino_secrets.h` is ignored by git. Use `arduino_secrets.h.template` to configure your local environment (SSID, PASS, TIMEZONE, VOLUMIO_HOST, VOLUMIO_PORT).
