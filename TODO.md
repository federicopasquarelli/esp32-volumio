# TODO

Open follow-ups from the October 2026 code review. Context and history are in [CLAUDE.md](CLAUDE.md).

## Improvements

- [ ] **Trim history comments** in `UiHandler.cpp`, `esp32-volumio.ino` and the
  headers down to a line each where needed. The history already lives in CLAUDE.md.

## Docs

- [ ] Remove the `xTaskCreatePinnedToCore` note from CLAUDE.md item 25: every call site it
  lists has been fixed or removed since.
