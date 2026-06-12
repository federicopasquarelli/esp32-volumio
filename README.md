# CYD Volumio Smart Clock

This project is an ESP32-based smart clock designed for the ESP32-2432S028 ("Cheap Yellow Display"). It integrates with [Volumio](https://volumio.com/) for music playback control and displays time and status information on a 2.8" SPI display.

## Features
- WiFi connectivity for Volumio control.
- Touch screen interface (XPT2046).
- Real-time status display (Song Title, Artist, Album, Playback Status).
- Volume control and mute toggle.

## Setup
To protect your WiFi and Volumio connection settings, this project uses a template file for secrets.

1.  Copy `arduino_secrets.h.template` to `arduino_secrets.h`:
    ```bash
    cp arduino_secrets.h.template arduino_secrets.h
    ```
2.  Edit `arduino_secrets.h` and update with your actual configuration:
    ```cpp
    #define SECRET_SSID "Your_WiFi_Name"
    #define SECRET_PASS "Your_WiFi_Password"
    #define SECRET_TIMEZONE "Your_Timezone_String"
    #define SECRET_VOLUMIO_HOST "volumio.local"
    #define SECRET_VOLUMIO_PORT 3000
    ```
3.  The `arduino_secrets.h` file is ignored by git to keep your credentials secure.
