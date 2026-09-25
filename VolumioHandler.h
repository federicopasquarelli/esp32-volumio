#ifndef VOLUMIO_HANDLER_H
#define VOLUMIO_HANDLER_H

#include <WebSocketsClient.h>

void setupVolumio();
void loopVolumio();
void togglePlayback();
void prevTrack();
void nextTrack();
void setVolume(int level);
void setRepeatMode(bool value, bool repeatSingle);
void setShuffle(bool shuffle);
void updateFolder(const char* uri);
void removeFromQueue(int index);
// Reboots / shuts down the Volumio device itself (its websocket "reboot" / "shutdown" events --
// there's no REST route for these, and no event to restart only the service). Return whether the
// command was actually sent, i.e. false while the websocket isn't connected.
bool restartVolumio();
bool shutdownVolumio();

#endif
