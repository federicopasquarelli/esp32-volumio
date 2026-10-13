#ifndef VOLUMIO_API_H
#define VOLUMIO_API_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Order matters: Library's folder actions extend this enum with ACTION_UPDATE after it.
enum QueueAction { QUEUE_PLAY, QUEUE_ADD, QUEUE_CLEAR_AND_PLAY };

String volumioUrl(const char* path);  // http://host:port/api/v1/<path>
String urlEncode(const char* msg);
int postJson(const char* path, const JsonDocument& body);
// Streams getQueue (no server-side paging, can be hundreds of KB): items [first, first+count)
// are parsed with `filter` and passed to on_item, the rest are only counted. Returns the queue
// length, or -1 on failure.
int streamQueue(int first, int count, const JsonDocument& filter,
                void (*on_item)(JsonObject item, void* ctx), void* ctx);
// Number of tracks in the queue, or -1 on failure.
int queueLength();
// Blocking -- call from a background task.
void runQueueAction(QueueAction action, const char* service, const char* uri, const char* title);

#endif
