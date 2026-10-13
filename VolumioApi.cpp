#include "VolumioApi.h"
#include "arduino_secrets.h"
#include <WiFi.h>
#include <HTTPClient.h>

String volumioUrl(const char* path) {
    return String("http://") + SECRET_VOLUMIO_HOST + ":" + SECRET_VOLUMIO_PORT + "/api/v1/" + path;
}

String urlEncode(const char* msg) {
    static const char hex[] = "0123456789ABCDEF";
    String out;
    out.reserve(strlen(msg) * 3);
    for (const char* p = msg; *p; p++) {
        unsigned char c = *p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

int postJson(const char* path, const JsonDocument& body) {
    WiFiClient client;
    HTTPClient req;
    req.begin(client, volumioUrl(path));
    req.setTimeout(8000);
    req.addHeader("Content-Type", "application/json");
    String payload;
    serializeJson(body, payload);
    int code = req.POST(payload);
    req.end();
    return code;
}

static bool isJsonSpace(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

static int nextNonSpace(Stream& s) {
    char c;
    while (s.readBytes(&c, 1) == 1) {
        if (!isJsonSpace(c)) return c;
    }
    return -1;
}

static int peekNonSpace(Stream& s) {
    unsigned long start = millis();
    while (millis() - start < 2000) {
        if (!s.available()) { delay(1); continue; }
        int c = s.peek();
        if (!isJsonSpace(c)) return c;
        s.read();
    }
    return -1;
}

// Consumes one JSON object/array/string without parsing it into memory.
static bool skipValue(Stream& s) {
    int depth = 0;
    bool in_str = false, esc = false;
    char c;
    while (s.readBytes(&c, 1) == 1) {
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') { in_str = false; if (depth == 0) return true; }
        } else if (c == '"') {
            in_str = true;
        } else if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            if (--depth == 0) return true;
        } else if (depth == 0 && !isJsonSpace(c)) {
            return false;  // queue items are objects; anything else is a malformed response
        }
    }
    return false;
}

static int walkQueueArray(Stream& s, int first, int count, const JsonDocument& filter,
                          void (*on_item)(JsonObject, void*), void* ctx) {
    int idx = 0;
    if (peekNonSpace(s) == ']') return 0;
    while (true) {
        if (on_item && idx >= first && idx < first + count) {
            JsonDocument doc;
            if (deserializeJson(doc, s, DeserializationOption::Filter(filter))) return -1;
            on_item(doc.as<JsonObject>(), ctx);
        } else if (!skipValue(s)) {
            return -1;
        }
        idx++;
        int sep = nextNonSpace(s);
        if (sep == ']') return idx;
        if (sep != ',') return -1;
    }
}

int streamQueue(int first, int count, const JsonDocument& filter,
                void (*on_item)(JsonObject item, void* ctx), void* ctx) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, volumioUrl("getQueue"));
    http.setTimeout(8000);
    int total = -1;
    if (http.GET() == HTTP_CODE_OK) {
        Stream& s = http.getStream();
        s.setTimeout(8000);
        if (s.find("\"queue\"") && s.find("[")) {
            s.setTimeout(2000);
            total = walkQueueArray(s, first, count, filter, on_item, ctx);
        }
    }
    http.end();
    return total;
}

int queueLength() {
    JsonDocument none;
    return streamQueue(0, 0, none, NULL, NULL);
}

void runQueueAction(QueueAction action, const char* service, const char* uri, const char* title) {
    JsonDocument body;
    body["service"] = service;
    body["uri"] = uri;
    body["title"] = title;

    switch (action) {
        case QUEUE_ADD:
            postJson("addToQueue", body);
            break;
        case QUEUE_CLEAR_AND_PLAY: {
            // Sent both flat and wrapped in "item" so either body shape is understood.
            JsonDocument wrapped = body;
            wrapped["item"] = body;
            postJson("replaceAndPlay", wrapped);
            break;
        }
        case QUEUE_PLAY: {
            // No REST "add and play": queue it, then play the first track added.
            int first = queueLength();
            if (first >= 0 && postJson("addToQueue", body) > 0) {
                WiFiClient client;
                HTTPClient req;
                req.begin(client, volumioUrl("commands/?cmd=play&N=") + String(first));
                req.setTimeout(8000);
                req.GET();
                req.end();
            }
            break;
        }
    }
}
