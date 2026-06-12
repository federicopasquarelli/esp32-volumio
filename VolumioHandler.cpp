#include "VolumioHandler.h"
#include "UiHandler.h"
#include <ArduinoJson.h>
#include "arduino_secrets.h"

WebSocketsClient ws;

void webSocketEvent(WStype_t t, uint8_t* p, size_t l) {
  if (t == WStype_CONNECTED) ws.sendTXT("42[\"getState\"]");
  if (t == WStype_TEXT && l > 2 && p[0] == '4' && p[1] == '2') {
    DynamicJsonDocument d(4096);
    deserializeJson(d, (char*)(p + 2));
    if (d[0] == "pushState") {
      updateVolumioUI(d[1]["title"] | "None", d[1]["artist"] | "None", d[1]["album"] | "None", d[1]["status"] == "play");
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
