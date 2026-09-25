#include "VolumioHandler.h"
#include "UiHandler.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "arduino_secrets.h"

WebSocketsClient ws;

// WebSocketsClient::loop() retries a connection that isn't up with a *blocking* connect() (a
// DNS/mDNS lookup of volumio.local plus up to 5s of TCP timeout) on the caller's own thread --
// here the Arduino loop(), which also runs LVGL and the touch reader. With the Raspberry off
// that meant a multi-second freeze every half second, so the whole UI, including the dropdown
// to reach Library/Queue/Lights, was effectively dead. loopVolumio() therefore only lets
// ws.loop() run when the socket is already connected, or when this probe has just confirmed
// Volumio is accepting connections (so the connect it makes will be quick). The probe is what
// eats the blocking time now, on its own task, where it can't hurt anything.
static volatile bool volumio_reachable = false;

// After a reboot/shutdown command Volumio's port stays open for a few more seconds while it goes
// down, so the probe would still report it reachable and ws.loop() would start a blocking connect
// against a dying host on the UI thread (the UI froze right after pressing Restart). Until this
// deadline nothing reconnects; the probe just idles and reachability reads as false.
#define SYSTEM_ACTION_HOLD_OFF_MS 30000
static volatile unsigned long reconnect_hold_until_ms = 0;
static volatile bool reconnect_hold = false;
static bool holdingOff() {
  if (reconnect_hold && (long)(millis() - reconnect_hold_until_ms) >= 0) reconnect_hold = false;
  return reconnect_hold;
}

static void probeTask(void*) {
  for (;;) {
    // Nothing to probe while connected, and deliberately no write to volumio_reachable here:
    // the DISCONNECTED handler below is what clears it, and this task racing it could undo that.
    // Short sleep so a drop is noticed (and, if Volumio is actually still up, re-confirmed and
    // reconnected) within a fraction of a second rather than leaving a visible gap.
    if (ws.isConnected()) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    if (holdingOff()) {
      volumio_reachable = false;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    WiFiClient probe;
    volumio_reachable = probe.connect(SECRET_VOLUMIO_HOST, SECRET_VOLUMIO_PORT, 1500);
    probe.stop();
    vTaskDelay(pdMS_TO_TICKS(volumio_reachable ? 1000 : 3000));
  }
}

void webSocketEvent(WStype_t t, uint8_t* p, size_t l) {
  if (t == WStype_CONNECTED) ws.sendTXT("42[\"getState\"]");
  // Lost an established connection: don't let ws.loop() go straight back to blocking on a dead
  // host until the probe has re-confirmed it's actually back.
  if (t == WStype_DISCONNECTED) volumio_reachable = false;
  if (t == WStype_TEXT && l > 2 && p[0] == '4' && p[1] == '2') {
    JsonDocument d;
    deserializeJson(d, (char*)(p + 2));
    if (d[0] == "pushState") {
      updateVolumioUI(
        d[1]["title"] | "None",
        d[1]["artist"] | "None",
        d[1]["album"] | "None",
        strcmp(d[1]["service"] | "", "airplay_emulation") == 0,
        d[1]["status"] == "play",
        d[1]["random"] | false,
        d[1]["repeat"] | false,
        d[1]["repeatSingle"] | false,
        (int)((d[1]["seek"] | 0) / 1000),  // seek is in ms, elapsed is shown in seconds
        d[1]["duration"] | 0
      );
      updateVolumeUI(d[1]["volume"] | 0);
    }
  }
}

void setupVolumio() {
  ws.begin(SECRET_VOLUMIO_HOST, SECRET_VOLUMIO_PORT, "/socket.io/?EIO=3&transport=websocket");
  ws.onEvent(webSocketEvent);
  xTaskCreatePinnedToCore(probeTask, "VolumioProbe", 6144, NULL, 1, NULL, 1);
}

// How long the socket has to stay down before the player screen says Volumio is unreachable --
// long enough that a quick drop-and-reconnect (or the moment right after boot, before the first
// connect) doesn't flash the notice.
#define OFFLINE_NOTICE_DELAY_MS 3000

void loopVolumio() {
  if (ws.isConnected() || (volumio_reachable && !holdingOff())) ws.loop();

  static bool down = false;
  static unsigned long down_since_ms = 0;
  if (ws.isConnected()) {
    down = false;
    setVolumioOffline(false);
  } else if (!down) {
    down = true;
    down_since_ms = millis();
  } else if (millis() - down_since_ms >= OFFLINE_NOTICE_DELAY_MS) {
    setVolumioOffline(true);
  }
}
void togglePlayback() { ws.sendTXT("42[\"toggle\"]"); }
void prevTrack() { ws.sendTXT("42[\"prev\"]"); }
void nextTrack() { ws.sendTXT("42[\"next\"]"); }

void setVolume(int v) {
  char cmd[64];
  sprintf(cmd, "42[\"volume\", %d]", v);
  ws.sendTXT(cmd);
}

void setRepeatMode(bool value, bool repeatSingle) {
  char cmd[128];
  sprintf(cmd, "42[\"setRepeat\", {\"value\": %s, \"repeatSingle\": %s}]", value ? "true" : "false", repeatSingle ? "true" : "false");
  ws.sendTXT(cmd);
}

void setShuffle(bool s) {
  char cmd[64];
  sprintf(cmd, "42[\"setRandom\", {\"value\": %s}]", s ? "true" : "false");
  ws.sendTXT(cmd);
}

void updateFolder(const char* uri) {
  JsonDocument d;
  d.add("updateDb");
  d.add(uri);
  String msg = "42";
  serializeJson(d, msg);
  ws.sendTXT(msg);
}

static bool sendSystemAction(const char* msg) {
  if (!ws.sendTXT(msg)) return false;
  volumio_reachable = false;
  reconnect_hold_until_ms = millis() + SYSTEM_ACTION_HOLD_OFF_MS;
  reconnect_hold = true;
  return true;
}
bool restartVolumio() { return sendSystemAction("42[\"reboot\"]"); }
bool shutdownVolumio() { return sendSystemAction("42[\"shutdown\"]"); }

void removeFromQueue(int index) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "42[\"removeFromQueue\",{\"value\":%d}]", index);
  ws.sendTXT(cmd);
}
