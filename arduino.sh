#!/usr/bin/env bash
# Common arduino-cli commands for this project (ESP32-2432S028 / "Cheap Yellow Display").
# Usage: ./arduino.sh <command>
#
# Context that matters when using this script (see CLAUDE.md for the full history):
#
# - PartitionScheme=huge_app is NOT optional. The default partition scheme is too small
#   for this sketch (~50% of flash with huge_app vs. 118%+ of the default scheme) --
#   compiling/uploading without it will fail to fit. Both FQBN values below already
#   include it; don't strip it when copy-pasting a raw arduino-cli command elsewhere.
# - The ESP32 core installed is pinned to esp32:esp32@3.3.11. Do NOT upgrade to
#   4.0.0-alpha1 -- its TLS stack is broken (every HTTPS handshake fails, including to
#   unrelated hosts like google.com; see CLAUDE.md's TLS quirk). If a future `arduino-cli
#   core upgrade` silently pulls the alpha, HTTPS (Tuya Lights) will break with no
#   obvious cause -- check `arduino-cli core list` first if that ever happens.
# - The serial port is normally NOT free. The user runs Arduino IDE with its own Serial
#   Monitor attached to /dev/ttyUSB0 most of the time. `upload` and `monitor` both need
#   exclusive access to the port and will fail with "port busy" (or hang) if the IDE (or
#   a previous `monitor`/`cat` left running) still has it open.
#     -> ALWAYS run `port-check` before `upload`/`monitor` if there's any doubt.
#     -> NEVER run `port-free` without asking the user first -- killing their IDE's
#        monitor out from under them mid-session has caused real disruption before. Ask,
#        or ask them to close their monitor themselves, rather than assuming it's safe.
# - `monitor` blocks the foreground forever (it's a live tail, not a one-shot read) and
#   needs Ctrl+C to exit. If you (Claude) need serial output non-interactively, prefer
#   redirecting a time-boxed capture instead, e.g.:
#     timeout 15 arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200 > /tmp/serial.log 2>&1
#   and then read /tmp/serial.log -- don't run the bare `monitor` subcommand from an
#   automated context, it will never return on its own.
# - Serial output only exists if the sketch currently calls Serial.begin() at all. Per
#   CLAUDE.md item 15, all Serial logging was removed project-wide by default -- if
#   `monitor` shows nothing, that's very possibly *expected*, not a broken port. Check
#   `grep -rn "Serial.begin" *.ino *.cpp` before assuming the capture pipeline is at fault.
# - `compile` alone is always safe to run without asking (it only reads this dir and
#   writes to the local build cache). `upload` and `flash` write to the physical device
#   and touch the shared serial port -- treat those as needing the same care as any other
#   action affecting hardware the user might currently be interacting with.

set -euo pipefail

# Full board id: vendor:arch:board[:menu_option=value,...]. The huge_app partition
# scheme suffix is what makes this differ from a bare `esp32:esp32:esp32`.
FQBN="esp32:esp32:esp32:PartitionScheme=huge_app"
# CH341 USB-serial adapter this board is normally connected through on this machine.
PORT="/dev/ttyUSB0"
# Matches whatever baud rate the sketch itself passes to Serial.begin() (currently none
# -- see the note above). Change this if a future debugging session re-adds Serial.begin()
# at a different rate, or `monitor` will just show garbage/nothing.
BAUD=115200
# Resolve the sketch directory from this script's own location rather than relying on
# the caller's cwd, so `./arduino.sh <cmd>` works the same regardless of where it's run
# from. arduino-cli identifies a sketch by directory (it expects a .ino file matching
# the directory name inside), not by an explicit file path.
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<EOF
Usage: ./arduino.sh <command>

Commands:
  compile         Compile the sketch (no upload). Safe to run anytime -- local only,
                  doesn't touch the device or the serial port.
  upload          Upload the LAST COMPILED build to $PORT. Needs the port free (see
                  port-check) and does not recompile first -- run 'compile' (or use
                  'flash') if the source changed since the last compile.
  flash           compile + upload in one step. What you want after any source edit.
  monitor         Open a live serial monitor on $PORT at $BAUD baud. Blocks until
                  Ctrl+C -- see the note above about using a timeout wrapper instead
                  when driving this non-interactively.
  port-check      Report whether $PORT is currently held by another process (e.g. the
                  Arduino IDE's own monitor) and by what PID, without touching anything.
                  Run this before upload/monitor if there's any doubt.
  port-free       Interactively kill whatever holds $PORT, after showing what it is and
                  asking for an explicit y/N confirmation. Never run this on the user's
                  behalf without asking them first -- their IDE monitor being open is
                  the normal state, not a bug.
  list-boards     List boards arduino-cli currently sees on any port (sanity check that
                  the device is connected/recognized at all).
  clean           Delete the local build/ dir AND arduino-cli's real incremental-build cache for
                  this sketch (~/.cache/arduino/sketches/<hash>/ -- that one, not build/, is what
                  actually needs clearing if a compile ever fails at the LINK step with no
                  source-level error: a stale/corrupt cached .o for the sketch itself got relinked
                  instead of rebuilt once, see CLAUDE.md). The next compile just rebuilds both.

Board/port used by this project:
  FQBN:  $FQBN
  Port:  $PORT
EOF
}

cmd_compile() {
    # --fqbn is required (this board isn't arduino-cli's default target). Passing $DIR
    # explicitly (rather than relying on cwd) is what makes this safe to call from
    # anywhere. Output includes flash/RAM usage percentages -- worth glancing at,
    # especially flash usage, since this project has hit size limits before (see
    # CLAUDE.md's huge_app note and the dram0_0_seg static-RAM ceiling hit twice this
    # project's history).
    arduino-cli compile --fqbn "$FQBN" "$DIR"
}

cmd_upload() {
    # -p is the serial port to flash over; must be free (see port-check). This uploads
    # whatever the last `compile` produced in build/ -- it does NOT re-read source files
    # or recompile, so an edit made after the last compile will silently NOT be reflected
    # on the device. Use `flash` instead if that's not guaranteed.
    arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$DIR"
}

cmd_flash() {
    # The safe default for "I changed code, get it onto the device": always compiles
    # fresh immediately before uploading, so there's no risk of uploading a stale build.
    cmd_compile
    cmd_upload
}

cmd_monitor() {
    # Live, blocking serial tail -- equivalent to Arduino IDE's Serial Monitor window.
    # Ctrl+C to exit; nothing is captured to a file unless the caller redirects stdout.
    # Requires the port to be free (close the IDE's own monitor first, or it fails with
    # "port busy"/resource-in-use). If driving this from an automated/non-interactive
    # context, wrap it in `timeout Ns ...` and redirect to a log file instead of calling
    # this function directly, since it otherwise never returns on its own.
    arduino-cli monitor -p "$PORT" -c baudrate="$BAUD"
}

cmd_port_check() {
    # Read-only: reports who (if anyone) holds the port, but takes no action. Always
    # safe to run, and the right first step before upload/monitor whenever the user's
    # own IDE might be attached (which is the common case on this machine).
    if command -v lsof >/dev/null 2>&1 && lsof "$PORT" >/dev/null 2>&1; then
        echo "$PORT is currently held by:"
        lsof "$PORT"
    else
        echo "$PORT is free."
    fi
}

cmd_port_free() {
    # Destructive-ish: kills another process's grip on the port. Only ever run this with
    # the user's explicit go-ahead for that specific moment -- most of the time the thing
    # holding the port is the user's own Arduino IDE Serial Monitor, and killing it out
    # from under them without asking has caused real disruption in this project before
    # (see CLAUDE.md's serial-port note). The confirmation prompt below is a last line of
    # defense, not a substitute for asking first.
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

cmd_list_boards() {
    # Lists every board arduino-cli currently detects on any port, with its guessed
    # FQBN. Useful as a first sanity check if upload/monitor can't find the device at
    # all (cable, driver, or the board being on a different /dev/ttyUSB* than expected).
    arduino-cli board list
}

cmd_clean() {
    # Removes the local build/ dir, then runs a compile with --clean, which tells arduino-cli to
    # ignore AND overwrite its own incremental-build cache for this sketch
    # (~/.cache/arduino/sketches/<hash>/ -- the one that actually matters, see CLAUDE.md's quirk
    # on this: a stale/corrupt cached object for the sketch itself got relinked once, causing a
    # link-time "undefined reference to setup()/loop()" with no source-level error anywhere).
    # Not part of the normal workflow -- plain `compile` already rebuilds whatever source-level
    # change it detects on its own. Only reach for this if a build is suspected stale/corrupt in a
    # way a fresh compile alone doesn't fix, e.g. exactly that symptom, or after switching
    # FQBN/board options and seeing odd leftover behavior.
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
