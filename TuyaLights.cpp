#include "TuyaLights.h"
#include "arduino_secrets.h"
#include "DisplayConfig.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <time.h>

#define TUYA_HOST         "openapi.tuyaeu.com"
#define TUYA_MAX_DEVICES  5
#define TUYA_NAME_LEN     40
#define TUYA_ID_LEN       32
#define TUYA_BTN_SIZE     130
// The code Tuya uses for a light's on/off switch in its status/command API (confirmed against
// two real "Extrastar A60" bulbs -- other device categories may use a different code, e.g.
// "switch_1" for a plain switch/socket).
#define TUYA_SWITCH_CODE  "switch_led"

struct TuyaDevice {
    char name[TUYA_NAME_LEN];
    char id[TUYA_ID_LEN];
    bool on;
};

static lv_obj_t* cont_lights = NULL;

// Cached across requests so we only re-authenticate once the token is close to expiring.
static char access_token[64] = "";
static unsigned long token_expires_at_ms = 0;

static TuyaDevice devices[TUYA_MAX_DEVICES];
static int device_count = 0;

enum TuyaOp { OP_LIST, OP_TOGGLE };
static TuyaOp current_op;
static int toggle_index = -1;
static bool toggle_target = false;

// Written by the task, read by loopTuyaLights(). Only one operation runs at a time.
static volatile bool fetching = false;
static volatile bool fetch_done = false;
static char fetch_error[96];

// --- Tuya request signing (HMAC-SHA256, Simple mode) ---
// https://developer.tuya.com/en/docs/iot/new-singnature -- every request is signed with:
//   stringToSign = Method + "\n" + Content-SHA256 + "\n" + "" + "\n" + path?query
//   str = client_id [+ access_token] + t + stringToSign
//   sign = HMAC-SHA256(str, client_secret), hex uppercase
// The token endpoint (Simple mode) signs without an access_token; every call after includes one.
// Content-SHA256 is over the real request body -- empty for GET, the JSON payload for POST.

static void sha256Hex(const uint8_t* data, size_t len, char out[65]) {
    uint8_t hash[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), data, len, hash);
    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hexd[hash[i] >> 4];
        out[i * 2 + 1] = hexd[hash[i] & 0xF];
    }
    out[64] = '\0';
}

static void hmacSha256HexUpper(const char* key, const char* msg, char out[65]) {
    uint8_t hash[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                     (const uint8_t*)key, strlen(key), (const uint8_t*)msg, strlen(msg), hash);
    static const char hexd[] = "0123456789ABCDEF";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hexd[hash[i] >> 4];
        out[i * 2 + 1] = hexd[hash[i] & 0xF];
    }
    out[64] = '\0';
}

// signAccessToken is NULL for the token endpoint itself, non-NULL for every call after.
static void buildSign(const char* method, const char* pathAndQuery, const char* body,
                       const char* signAccessToken, char signOut[65], char tOut[16]) {
    char bodyHash[65];
    sha256Hex((const uint8_t*)body, strlen(body), bodyHash);

    char stringToSign[300];
    snprintf(stringToSign, sizeof(stringToSign), "%s\n%s\n\n%s", method, bodyHash, pathAndQuery);

    snprintf(tOut, 16, "%llu", (unsigned long long)time(nullptr) * 1000ULL);

    char str[512];
    if (signAccessToken && signAccessToken[0]) {
        snprintf(str, sizeof(str), "%s%s%s%s", SECRET_TUYA_CLIENT_ID, signAccessToken, tOut, stringToSign);
    } else {
        snprintf(str, sizeof(str), "%s%s%s", SECRET_TUYA_CLIENT_ID, tOut, stringToSign);
    }
    hmacSha256HexUpper(SECRET_TUYA_CLIENT_KEY, str, signOut);
}

// Signed request against the Tuya Cloud API (GET when body is "", POST with a JSON body
// otherwise). accessTokenForSign is also sent as the access_token header when non-NULL
// (everything except the token endpoint itself).
static bool tuyaRequest(const char* method, const char* pathAndQuery, const char* body,
                         const char* accessTokenForSign, JsonDocument& doc, char* errOut, size_t errLen) {
    char sign[65], t[16];
    buildSign(method, pathAndQuery, body, accessTokenForSign, sign, t);

    WiFiClientSecure client;
    // Tuya's cert isn't pinned -- acceptable for this personal/home use.
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    String url = String("https://") + TUYA_HOST + pathAndQuery;
    if (!http.begin(client, url)) {
        strlcpy(errOut, "Connection failed", errLen);
        return false;
    }
    http.addHeader("client_id", SECRET_TUYA_CLIENT_ID);
    http.addHeader("sign", sign);
    http.addHeader("t", t);
    http.addHeader("sign_method", "HMAC-SHA256");
    if (accessTokenForSign && accessTokenForSign[0]) http.addHeader("access_token", accessTokenForSign);

    int code;
    if (body[0]) {
        http.addHeader("Content-Type", "application/json");
        code = http.POST((uint8_t*)body, strlen(body));
    } else {
        code = http.GET();
    }

    if (code != HTTP_CODE_OK) {
        if (code < 0) snprintf(errOut, errLen, "%s", http.errorToString(code).c_str());
        else snprintf(errOut, errLen, "HTTP %d", code);
        http.end();
        return false;
    }

    DeserializationError jerr = deserializeJson(doc, http.getStream());
    http.end();
    if (jerr) {
        strlcpy(errOut, "Invalid response", errLen);
        return false;
    }
    if (!(doc["success"] | false)) {
        const char* msg = doc["msg"] | "Tuya error";
        strlcpy(errOut, msg, errLen);
        return false;
    }
    return true;
}

static bool tuyaGet(const char* pathAndQuery, const char* accessTokenForSign, JsonDocument& doc,
                     char* errOut, size_t errLen) {
    return tuyaRequest("GET", pathAndQuery, "", accessTokenForSign, doc, errOut, errLen);
}

// Fetches a fresh access token if we don't have one yet, or it's about to expire.
static bool ensureToken(char* errOut, size_t errLen) {
    if (access_token[0] && millis() < token_expires_at_ms) return true;

    JsonDocument doc;
    if (!tuyaGet("/v1.0/token?grant_type=1", NULL, doc, errOut, errLen)) return false;

    const char* tok = doc["result"]["access_token"] | "";
    if (!tok[0]) {
        strlcpy(errOut, "No token in response", errLen);
        return false;
    }
    strlcpy(access_token, tok, sizeof(access_token));

    long expireSec = doc["result"]["expire_time"] | 0;
    unsigned long marginMs = 60000;  // refresh a bit early rather than racing the exact expiry
    unsigned long lifetimeMs = (unsigned long)expireSec * 1000UL;
    if (lifetimeMs > marginMs) lifetimeMs -= marginMs;
    token_expires_at_ms = millis() + lifetimeMs;

    return true;
}

// Same active/inactive language as the player's shuffle/repeat buttons: solid accent when on,
// and black (same as the screen background, so it reads as transparent/outline-only) with just
// an accent border when off.
static void styleDeviceButton(lv_obj_t* btn, bool on) {
    lv_obj_set_size(btn, TUYA_BTN_SIZE, TUYA_BTN_SIZE);
    lv_obj_set_style_radius(btn, 16, 0);
    if (on) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x19A34A), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_ACCENT), 0);
    }
}

static void setStatus(const char* text) {
    lv_obj_clean(cont_lights);
    lv_obj_t* l = lv_label_create(cont_lights);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0xAAAAAA), 0);
}

static void showError(const char* error) {
    lv_obj_clean(cont_lights);
    char msg[110];
    snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s", error);
    lv_obj_t* l = lv_label_create(cont_lights);
    lv_label_set_text(l, msg);
    lv_obj_set_style_text_color(l, lv_color_hex(0xAAAAAA), 0);
}

static void startTask();

// A tap that lands while another request is still in flight isn't dropped -- it's remembered
// here and fired the moment that request finishes (loopTuyaLights()), so flipping two lights in
// quick succession doesn't require tapping the second one twice. Only one request runs at a
// time on purpose: this core's TLS handshake needs a big contiguous heap block (see the TLS
// quirk in CLAUDE.md), and running two at once risks starving it again.
static int pending_index = -1;

static void startToggle(int idx) {
    current_op = OP_TOGGLE;
    toggle_index = idx;
    toggle_target = !devices[idx].on;
    startTask();
}

static void device_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= device_count) return;
    if (fetching) {
        pending_index = idx;  // replaces any earlier still-pending tap
        return;
    }
    startToggle(idx);
}

static void showList() {
    lv_obj_clean(cont_lights);
    if (device_count == 0) {
        lv_obj_t* l = lv_label_create(cont_lights);
        lv_label_set_text(l, "No devices found");
        lv_obj_set_style_text_color(l, lv_color_hex(0xAAAAAA), 0);
        return;
    }
    for (int i = 0; i < device_count; i++) {
        lv_obj_t* b = lv_button_create(cont_lights);
        styleDeviceButton(b, devices[i].on);
        lv_obj_add_event_cb(b, device_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* icon = lv_label_create(b);
        lv_label_set_text(icon, LV_SYMBOL_POWER);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 16);

        lv_obj_t* name = lv_label_create(b);
        lv_label_set_text(name, devices[i].name);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(name, TUYA_BTN_SIZE - 16);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -14);
    }
}

// Populates devices[0..device_count) with id/name, then looks up each one's current switch
// state -- the list endpoint only has static metadata, not live status.
static bool fetchDeviceList(char* errOut, size_t errLen) {
    device_count = 0;

    JsonDocument doc;
    if (!tuyaGet("/v2.0/cloud/thing/device?page_size=5", access_token, doc, errOut, errLen)) return false;

    // Tuya's device-list shape has moved around across API versions -- accept either "result"
    // being the array directly (confirmed live), or "result.list" wrapping it.
    JsonArray arr = doc["result"]["list"].is<JsonArray>() ? doc["result"]["list"].as<JsonArray>()
                                                            : doc["result"].as<JsonArray>();
    for (JsonObject d : arr) {
        if (device_count >= TUYA_MAX_DEVICES) break;
        TuyaDevice& dst = devices[device_count++];
        const char* name = d["customName"] | (d["name"] | "Unknown");
        strlcpy(dst.name, name, sizeof(dst.name));
        strlcpy(dst.id, d["id"] | "", sizeof(dst.id));
        dst.on = false;
    }

    for (int i = 0; i < device_count; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/v1.0/iot-03/devices/%s/status", devices[i].id);
        JsonDocument statusDoc;
        char statusErr[48];
        if (!tuyaGet(path, access_token, statusDoc, statusErr, sizeof(statusErr))) continue;
        for (JsonObject s : statusDoc["result"].as<JsonArray>()) {
            if (strcmp(s["code"] | "", TUYA_SWITCH_CODE) == 0) {
                devices[i].on = s["value"] | false;
                break;
            }
        }
    }

    return true;
}

static bool sendToggle(int idx, bool target, char* errOut, size_t errLen) {
    char path[64];
    snprintf(path, sizeof(path), "/v1.0/iot-03/devices/%s/commands", devices[idx].id);

    JsonDocument body;
    JsonObject cmd = body["commands"].add<JsonObject>();
    cmd["code"] = TUYA_SWITCH_CODE;
    cmd["value"] = target;
    String payload;
    serializeJson(body, payload);

    JsonDocument respDoc;
    if (!tuyaRequest("POST", path, payload.c_str(), access_token, respDoc, errOut, errLen)) return false;

    devices[idx].on = target;
    return true;
}

static void fetchTask(void*) {
    fetch_error[0] = '\0';

    if (ensureToken(fetch_error, sizeof(fetch_error))) {
        if (current_op == OP_LIST) {
            fetchDeviceList(fetch_error, sizeof(fetch_error));
        } else if (toggle_index >= 0 && toggle_index < device_count) {
            sendToggle(toggle_index, toggle_target, fetch_error, sizeof(fetch_error));
        }
    }

    fetch_done = true;
    vTaskDelete(NULL);
}

static void startTask() {
    if (fetching) return;
    fetching = true;
    // Signing + TLS handshake + JSON parsing all want real stack space.
    xTaskCreatePinnedToCore(fetchTask, "TuyaFetch", 16384, NULL, 1, NULL, 1);
}

void setupTuyaLights(lv_obj_t* parent) {
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    // Plain flex container instead of a list: big buttons, centered, wrapping to a new row if
    // there isn't room (works whether there are 1, 2, or up to TUYA_MAX_DEVICES of them).
    cont_lights = lv_obj_create(parent);
    lv_obj_set_size(cont_lights, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cont_lights, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(cont_lights, 0, 0);
    lv_obj_set_style_pad_all(cont_lights, 12, 0);
    lv_obj_set_style_pad_column(cont_lights, 16, 0);
    lv_obj_set_style_pad_row(cont_lights, 16, 0);
    lv_obj_remove_flag(cont_lights, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_lights, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont_lights, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

void refreshTuyaLights() {
    if (fetching) return;
    pending_index = -1;  // a fresh full reload supersedes any toggle queued from before
    current_op = OP_LIST;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startTask();
}

void loopTuyaLights() {
    if (!fetch_done) return;
    fetch_done = false;
    fetching = false;

    if (fetch_error[0]) showError(fetch_error);
    else showList();

    if (pending_index >= 0) {
        int idx = pending_index;
        pending_index = -1;
        if (idx < device_count) startToggle(idx);
    }
}
