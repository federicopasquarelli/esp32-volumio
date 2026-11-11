#!/usr/bin/env bash
# arduino-cli shortcuts for this sketch (ESP32-2432S028 "Cheap Yellow Display").
# Usage: ./arduino.sh <command>
#
# - The Huge APP partition scheme is required: the sketch doesn't fit the default one.
# - Use the esp32:esp32 3.3.x core; the 4.0.0 alpha's TLS stack is broken.
# - upload/monitor need the serial port to themselves: close any other serial monitor first
#   (port-check shows what holds it).
# - The sketch doesn't log to Serial by default, so an empty monitor is expected.
#
# Override the port with PORT=/dev/ttyACM0 ./arduino.sh flash

set -euo pipefail

FQBN="esp32:esp32:esp32:PartitionScheme=huge_app"
PORT="${PORT:-/dev/ttyUSB0}"
BAUD="${BAUD:-115200}"
# arduino-cli identifies a sketch by its directory, so resolve it from this script's location.
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<EOF
Usage: ./arduino.sh <command>

Commands:
  compile      Compile the sketch.
  upload       Upload the last compiled build to $PORT (doesn't recompile).
  flash        compile + upload.
  monitor      Serial monitor on $PORT at $BAUD baud (Ctrl+C to exit).
  port-check   Show which process, if any, holds $PORT.
  port-free    Kill the process holding $PORT, after asking.
  list-boards  List the boards arduino-cli can see.
  clean        Delete the build caches and recompile from scratch. Use it if a build fails at
               the link step with no source error (a stale cached object).

FQBN: $FQBN
Port: $PORT
EOF
}

cmd_compile() { arduino-cli compile --fqbn "$FQBN" "$DIR"; }

cmd_upload() { arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$DIR"; }

cmd_flash() {
    cmd_compile
    cmd_upload
}

cmd_monitor() { arduino-cli monitor -p "$PORT" -c baudrate="$BAUD"; }

cmd_port_check() {
    if command -v lsof >/dev/null 2>&1 && lsof "$PORT" >/dev/null 2>&1; then
        echo "$PORT is currently held by:"
        lsof "$PORT"
    else
        echo "$PORT is free."
    fi
}

cmd_port_free() {
    if ! command -v lsof >/dev/null 2>&1 || ! lsof "$PORT" >/dev/null 2>&1; then
        echo "$PORT is already free."
        return 0
    fi
    lsof "$PORT"
    read -r -p "Kill the process(es) above holding $PORT? [y/N] " reply
    if [[ "$reply" =~ ^[Yy]$ ]]; then
        lsof -t "$PORT" | xargs -r kill
        echo "Done."
    else
        echo "Aborted."
    fi
}

cmd_list_boards() { arduino-cli board list; }

cmd_clean() {
    # --clean ignores and overwrites arduino-cli's own cache (~/.cache/arduino/sketches/<hash>/).
    rm -rf "$DIR/build"
    arduino-cli compile --clean --fqbn "$FQBN" "$DIR"
}

case "${1:-}" in
    compile)     cmd_compile ;;
    upload)      cmd_upload ;;
    flash)       cmd_flash ;;
    monitor)     cmd_monitor ;;
    port-check)  cmd_port_check ;;
    port-free)   cmd_port_free ;;
    list-boards) cmd_list_boards ;;
    clean)       cmd_clean ;;
    *)           usage; exit 1 ;;
esac
