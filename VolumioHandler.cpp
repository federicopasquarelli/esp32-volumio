#include "VolumioHandler.h"
#include "UiHandler.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "arduino_secrets.h"

WebSocketsClient ws;

// WebSocketsClient::loop() reconnects with a *blocking* connect() (DNS/mDNS lookup plus up to 5s
// of TCP timeout) on the caller's thread, which here also runs LVGL and touch: with Volumio off,
// the UI froze every half second. So loopVolumio() only calls ws.loop() while connected, or when
// this background probe has just seen Volumio's port accept a connection.
static volatile bool volumio_reachable = false;

// After a reboot/shutdown Volumio's port stays open for a few seconds while it goes down, and a
// reconnect then would block the UI. Until this deadline nothing reconnects.
#define SYSTEM_ACTION_HOLD_OFF_MS 30000
static volatile unsigned long reconnect_hold_until_ms = 0;
static volatile bool reconnect_hold = false;
static bool holdingOff() {
  if (reconnect_hold && (long)(millis() - reconnect_hold_until_ms) >= 0) reconnect_hold = false;
  return reconnect_hold;
}

static void probeTask(void*) {
  for (;;) {
    // Nothing to probe while connected. No write to volumio_reachable here: the DISCONNECTED
    // handler clears it, and racing it could undo that.
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

void moveInQueue(int from, int to) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "42[\"moveQueue\",{\"from\":%d,\"to\":%d}]", from, to);
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

bool updateLibraryDb() { return ws.sendTXT("42[\"updateDb\"]"); }

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

// ["<event>",{"name":"<name>"}], built with ArduinoJson so the name is escaped.
static void sendPlaylistEvent(const char* event, const char* name) {
  JsonDocument msg;
  msg.add(event);
  msg.add<JsonObject>()["name"] = name;
  String json;
  serializeJson(msg, json);
  String frame = "42" + json;
  ws.sendTXT(frame);
}
void playPlaylist(const char* name) { sendPlaylistEvent("playPlaylist", name); }
void enqueuePlaylist(const char* name) { sendPlaylistEvent("enqueue", name); }
