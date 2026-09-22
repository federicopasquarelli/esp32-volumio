#include "VolumioHandler.h"
#include "UiHandler.h"
#include <ArduinoJson.h>
#include "arduino_secrets.h"

WebSocketsClient ws;

void webSocketEvent(WStype_t t, uint8_t* p, size_t l) {
  if (t == WStype_CONNECTED) { Serial.println("[WS] connected"); ws.sendTXT("42[\"getState\"]"); }
  if (t == WStype_DISCONNECTED) Serial.println("[WS] disconnected");
  if (t == WStype_TEXT && l > 2 && p[0] == '4' && p[1] == '2') {
    JsonDocument d;
    deserializeJson(d, (char*)(p + 2));
    if (d[0] == "pushState") {
      updateVolumioUI(
        d[1]["title"] | "None",
        d[1]["artist"] | "None",
        d[1]["album"] | "None",
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
}

void loopVolumio() { ws.loop(); }
void togglePlayback() { ws.sendTXT("42[\"toggle\"]"); }
void prevTrack() { ws.sendTXT("42[\"prev\"]"); }
void nextTrack() { ws.sendTXT("42[\"next\"]"); }

void setVolume(int v) {
  char cmd[64];
  sprintf(cmd, "42[\"volume\", %d]", v);
  ws.sendTXT(cmd);
}

void mute() { ws.sendTXT("42[\"mute\"]"); }
void unmute() { ws.sendTXT("42[\"unmute\"]"); }

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

void removeFromQueue(int index) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "42[\"removeFromQueue\",{\"value\":%d}]", index);
  ws.sendTXT(cmd);
}
