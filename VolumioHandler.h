#ifndef VOLUMIO_HANDLER_H
#define VOLUMIO_HANDLER_H

#include <WebSocketsClient.h>

void setupVolumio();
void loopVolumio();
void togglePlayback();
void prevTrack();
void nextTrack();
void setVolume(int level);
void mute();
void unmute();
void setRepeatMode(bool value, bool repeatSingle);
void setShuffle(bool shuffle);
void updateFolder(const char* uri);
void removeFromQueue(int index);

#endif
