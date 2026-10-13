# TODO

Open follow-ups from the October 2026 code review. Context and history are in [CLAUDE.md](CLAUDE.md).

## Bugs

- [ ] **Check `xTaskCreatePinnedToCore()`'s return value in [TuyaLights.cpp](TuyaLights.cpp)
  `startTask()`**: on failure `fetching` stays `true`, so Lights sits on "Loading..." and no
  further Tuya request can ever start.

  Same bug class as CLAUDE.md item 25's follow-up; it's the last call site that doesn't check it.

## Improvements

- [ ] **Trim history comments** in `TuyaLights.cpp`, `UiHandler.cpp`, `arduino32.ino` and the
  headers down to a line each where needed. The history already lives in CLAUDE.md.

## Docs

- [ ] Remove the `xTaskCreatePinnedToCore` note from CLAUDE.md item 25 once the call site
  above is fixed (it lists sites that were fixed since).
