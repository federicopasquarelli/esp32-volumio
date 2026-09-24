#include "TuyaLights.h"
#include "arduino_secrets.h"
#include "DisplayConfig.h"
#include "CustomFonts.h"
#include "UiHandler.h"
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
    // Brightness lives in different places depending on work_mode: bright_value_v2 for "white",
    // colour_data_v2's v for "colour" (h/s kept so a brightness change can resend them as-is).
    bool colour_mode;
    int hue, sat;
    int bright_pct;  // 1..100, both codes map to this as raw = pct * 10
};

static lv_obj_t* cont_lights = NULL;

// Cached across requests so we only touch the auth endpoints once the token is close to
// expiring (access tokens last 2h) -- opening the Lights screen normally costs zero auth calls.
static char access_token[64] = "";
static char refresh_token[64] = "";
static unsigned long token_expires_at_ms = 0;

static TuyaDevice devices[TUYA_MAX_DEVICES];
static int device_count = 0;

enum TuyaOp { OP_AUTH, OP_LIST, OP_TOGGLE, OP_BRIGHTNESS, OP_COLOUR };
static TuyaOp current_op;
static int toggle_index = -1;
static bool toggle_target = false;
static int bright_index = -1;
static int bright_pct_target = 0;
static int colour_index = -1;
static int colour_hue_target = 0;  // 0..360, or -1 for "white"
static int colour_pct_target = 0;

// Colour palette on the brightness page: eleven hues at full saturation plus white (which
// switches the light back to white mode). Tuya hue is 0..360. Laid out as two rows of six.
#define PALETTE_COUNT 12
static const int palette_hues[PALETTE_COUNT] = { 0, 28, 55, 90, 130, 180, 210, 240, 275, 315, 345, -1 };
static lv_obj_t* swatches[PALETTE_COUNT];

// Long-pressing a light opens a per-light brightness page (a hidden screen, no dropdown entry).
static lv_obj_t* lights_screen = NULL;
static lv_obj_t* brightness_screen = NULL;
static lv_obj_t* slider_bright = NULL;
static lv_obj_t* label_bright_name = NULL;
static lv_obj_t* label_bright_pct = NULL;
static lv_obj_t* label_bright_status = NULL;
static int brightness_dev = -1;
// Set by the brightness page's Back button so the Lights screen's on_show knows to just redraw
// what it already has instead of reloading everything over the network.
static bool returning_from_brightness = false;
// LVGL still sends CLICKED on release after a long press, so remember the press was consumed.
static bool long_press_handled = false;

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
// (everything except the token endpoints themselves). httpCodeOut, if given, receives the raw
// HTTP status (0 if the request never reached the server) -- tuyaCall() uses it to notice a 401
// and retry with a fresh token.
static bool tuyaRequest(const char* method, const char* pathAndQuery, const char* body,
                         const char* accessTokenForSign, JsonDocument& doc, char* errOut, size_t errLen,
                         int* httpCodeOut = NULL) {
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
    if (httpCodeOut) *httpCodeOut = code;

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

// Common to both the login and refresh responses: {"result":{"access_token","refresh_token",
// "expire_time",...}}. The refresh_token Tuya hands back is itself single-use, so it always
// replaces the one we had, from either call.
static bool storeTokenResponse(JsonDocument& doc, char* errOut, size_t errLen) {
    const char* tok = doc["result"]["access_token"] | "";
    if (!tok[0]) {
        strlcpy(errOut, "No token in response", errLen);
        return false;
    }
    strlcpy(access_token, tok, sizeof(access_token));
    strlcpy(refresh_token, doc["result"]["refresh_token"] | "", sizeof(refresh_token));

    long expireSec = doc["result"]["expire_time"] | 0;
    unsigned long marginMs = 60000;  // renew a bit early rather than racing the exact expiry
    unsigned long lifetimeMs = (unsigned long)expireSec * 1000UL;
    if (lifetimeMs > marginMs) lifetimeMs -= marginMs;
    token_expires_at_ms = millis() + lifetimeMs;
    return true;
}

// Full login (Simple mode signing, no access_token). Only needed the very first time, or as a
// fallback if refreshAccessToken() fails (e.g. the refresh_token was already used, or never
// existed).
static bool authenticateFresh(char* errOut, size_t errLen) {
    JsonDocument doc;
    if (!tuyaGet("/v1.0/token?grant_type=1", NULL, doc, errOut, errLen)) return false;
    return storeTokenResponse(doc, errOut, errLen);
}

// https://developer.tuya.com/en/docs/cloud/80bb968f1d -- GET /v1.0/token/{refresh_token}, no
// grant_type, no access_token header (same as the login call). Cheaper than a fresh login and
// the whole point of holding a refresh_token: the access token expires every 2h, and without
// this every renewal would otherwise fall back to a full login.
static bool refreshAccessToken(char* errOut, size_t errLen) {
    if (!refresh_token[0]) return false;
    char path[96];
    snprintf(path, sizeof(path), "/v1.0/token/%s", refresh_token);
    JsonDocument doc;
    if (!tuyaGet(path, NULL, doc, errOut, errLen)) return false;
    return storeTokenResponse(doc, errOut, errLen);
}

// Gets us a usable access token: the cached one if it's still fresh, otherwise a refresh, falling
// back to a full login if refreshing didn't work (or there was nothing to refresh yet).
static bool ensureToken(char* errOut, size_t errLen) {
    if (access_token[0] && millis() < token_expires_at_ms) return true;
    if (refreshAccessToken(errOut, errLen)) return true;
    return authenticateFresh(errOut, errLen);
}

// Runs a signed, authenticated call: makes sure we have a token, sends the request, and if Tuya
// rejects it with 401 (token revoked, clock skew, refreshed elsewhere -- ensureToken() already
// keeps it fresh in the common case, so this is the rare-case safety net) forces a new one and
// retries exactly once, rather than surfacing the failure to the user.
static bool tuyaCall(const char* method, const char* pathAndQuery, const char* body,
                      JsonDocument& doc, char* errOut, size_t errLen) {
    if (!ensureToken(errOut, errLen)) return false;

    int httpCode = 0;
    if (tuyaRequest(method, pathAndQuery, body, access_token, doc, errOut, errLen, &httpCode)) return true;
    if (httpCode != 401) return false;

    access_token[0] = '\0';  // don't let ensureToken() think this one's still good
    if (!ensureToken(errOut, errLen)) return false;
    doc.clear();
    return tuyaRequest(method, pathAndQuery, body, access_token, doc, errOut, errLen, &httpCode);
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

// A tap (or screen open) that lands while another request is still in flight isn't dropped --
// it's remembered here and fired the moment that request finishes (loopTuyaLights()). Covers:
// flipping two lights in quick succession without tapping the second one twice, releasing the
// brightness slider while something else is in flight, and opening the Lights screen while the
// boot-time pre-auth (startTuyaAuth()) is still running.
// Only one request runs at a time on purpose: this core's TLS handshake needs a big contiguous
// heap block (see the TLS quirk in CLAUDE.md), and running two at once risks starving it again.
static int pending_index = -1;
static bool pending_list_refresh = false;
static int pending_bright_index = -1;
static int pending_bright_pct = 0;
static int pending_colour_index = -1;
static int pending_colour_hue = 0;
static int pending_colour_pct = 0;

static void startToggle(int idx) {
    current_op = OP_TOGGLE;
    toggle_index = idx;
    toggle_target = !devices[idx].on;
    startTask();
}

// What colour to hand LVGL so the panel actually shows (r,g,b). See DISPLAY_BYTE_SWAPPED in
// DisplayConfig.h: every colour reaches the panel with its two RGB565 bytes swapped, which is
// harmless for the accent/greys but wrong for a palette whose whole point is showing the real
// colour. Pre-swapping here cancels it out exactly (LVGL packs back to the same 16 bits).
static lv_color_t panelColor(uint8_t r, uint8_t g, uint8_t b) {
#if DISPLAY_BYTE_SWAPPED
    uint16_t t = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    uint16_t w = (uint16_t)((t >> 8) | (t << 8));
    return lv_color_make(((w >> 11) & 0x1F) << 3, ((w >> 5) & 0x3F) << 2, (w & 0x1F) << 3);
#else
    return lv_color_make(r, g, b);
#endif
}

// Full-saturation, full-value colour for a Tuya hue (0..360); a negative hue is white.
static lv_color_t hueColor(int h) {
    if (h < 0) return panelColor(255, 255, 255);
    int f = (h % 60) * 255 / 60;
    uint8_t r, g, b;
    switch ((h / 60) % 6) {
        case 0:  r = 255;     g = f;       b = 0;       break;
        case 1:  r = 255 - f; g = 255;     b = 0;       break;
        case 2:  r = 0;       g = 255;     b = f;       break;
        case 3:  r = 0;       g = 255 - f; b = 255;     break;
        case 4:  r = f;       g = 0;       b = 255;     break;
        default: r = 255;     g = 0;       b = 255 - f; break;
    }
    return panelColor(r, g, b);
}

static void setSwatchSelected(int selected) {
    for (int i = 0; i < PALETTE_COUNT; i++) {
        lv_obj_set_style_border_width(swatches[i], i == selected ? 3 : 0, 0);
    }
}

// Marks the swatch matching the light's current colour: white when it's in white mode, otherwise
// the palette hue closest to its actual hue.
static void updateSwatchSelection(int dev) {
    int selected = PALETTE_COUNT - 1;
    if (dev >= 0 && dev < device_count && devices[dev].colour_mode) {
        int best = 1000;
        for (int i = 0; i < PALETTE_COUNT - 1; i++) {
            int diff = abs(devices[dev].hue - palette_hues[i]);
            if (diff > 180) diff = 360 - diff;
            if (diff < best) { best = diff; selected = i; }
        }
    }
    setSwatchSelected(selected);
}

static void startColour(int idx, int hue, int pct) {
    current_op = OP_COLOUR;
    colour_index = idx;
    colour_hue_target = hue;
    colour_pct_target = pct;
    startTask();
}

static void requestColour(int idx, int hue, int pct) {
    if (fetching) {
        pending_colour_index = idx;  // last tap wins
        pending_colour_hue = hue;
        pending_colour_pct = pct;
        return;
    }
    startColour(idx, hue, pct);
}

static void swatch_cb(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= PALETTE_COUNT || brightness_dev < 0 || brightness_dev >= device_count) return;
    setSwatchSelected(i);  // answer the tap at once; loopTuyaLights() restores it if the command fails
    requestColour(brightness_dev, palette_hues[i], lv_slider_get_value(slider_bright));
}

static void startBrightness(int idx, int pct) {
    current_op = OP_BRIGHTNESS;
    bright_index = idx;
    bright_pct_target = pct;
    startTask();
}

static void requestBrightness(int idx, int pct) {
    if (fetching) {
        pending_bright_index = idx;  // last release wins
        pending_bright_pct = pct;
        return;
    }
    startBrightness(idx, pct);
}

static void openBrightness(int idx) {
    brightness_dev = idx;
    lv_label_set_text(label_bright_name, devices[idx].name);
    lv_slider_set_value(slider_bright, devices[idx].bright_pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(label_bright_pct, "%d%%", devices[idx].bright_pct);
    lv_label_set_text(label_bright_status, "");
    updateSwatchSelection(idx);
    showScreen(brightness_screen);
}

// While dragging only the readout follows; the command goes out once, on release -- one request
// per pixel of drag would just queue up TLS round trips behind each other.
static void bright_slider_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int pct = lv_slider_get_value(slider_bright);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_label_set_text_fmt(label_bright_pct, "%d%%", pct);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && brightness_dev >= 0 &&
               brightness_dev < device_count) {
        requestBrightness(brightness_dev, pct);
    }
}

static void bright_back_cb(lv_event_t*) {
    returning_from_brightness = true;
    showScreen(lights_screen);
}

static void device_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= device_count) return;

    if (code == LV_EVENT_PRESSED) {
        long_press_handled = false;
    } else if (code == LV_EVENT_LONG_PRESSED) {
        long_press_handled = true;
        openBrightness(idx);
    } else if (code == LV_EVENT_CLICKED) {
        if (long_press_handled) {
            long_press_handled = false;
            return;
        }
        if (fetching) {
            pending_index = idx;  // replaces any earlier still-pending tap
            return;
        }
        startToggle(idx);
    }
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
        lv_obj_add_event_cb(b, device_cb, LV_EVENT_ALL, (void*)(intptr_t)i);

        lv_obj_t* icon = lv_label_create(b);
        lv_label_set_text(icon, LV_SYMBOL_POWER);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_ext_32, 0);
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
    if (!tuyaCall("GET", "/v2.0/cloud/thing/device?page_size=5", "", doc, errOut, errLen)) return false;

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
        dst.colour_mode = false;
        dst.hue = 0;
        dst.sat = 1000;
        dst.bright_pct = 100;
    }

    for (int i = 0; i < device_count; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/v1.0/iot-03/devices/%s/status", devices[i].id);
        JsonDocument statusDoc;
        char statusErr[48];
        if (!tuyaCall("GET", path, "", statusDoc, statusErr, sizeof(statusErr))) continue;
        int white_raw = 1000, colour_v = 1000;
        for (JsonObject s : statusDoc["result"].as<JsonArray>()) {
            const char* code = s["code"] | "";
            if (strcmp(code, TUYA_SWITCH_CODE) == 0) {
                devices[i].on = s["value"] | false;
            } else if (strcmp(code, "work_mode") == 0) {
                devices[i].colour_mode = strcmp(s["value"] | "", "colour") == 0;
            } else if (strcmp(code, "bright_value_v2") == 0) {
                white_raw = s["value"] | 1000;
            } else if (strcmp(code, "colour_data_v2") == 0) {
                // Comes back as a JSON *string* in status (even though commands take an object).
                JsonDocument colour;
                if (!deserializeJson(colour, (const char*)(s["value"] | "{}"))) {
                    devices[i].hue = colour["h"] | 0;
                    devices[i].sat = colour["s"] | 1000;
                    colour_v = colour["v"] | 1000;
                }
            }
        }
        int pct = (devices[i].colour_mode ? colour_v : white_raw) / 10;
        devices[i].bright_pct = pct < 1 ? 1 : (pct > 100 ? 100 : pct);
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
    if (!tuyaCall("POST", path, payload.c_str(), respDoc, errOut, errLen)) return false;

    devices[idx].on = target;
    return true;
}

// The official docs (standard instruction set for lights, category dj) give bright_value_v2 as
// 10..1000 and colour_data_v2 as an object {"h","s","v"} with v 0..1000, but don't say which one
// governs brightness in which work_mode, nor whether either turns an off light on -- so this
// follows what each code is for (colour mode -> v, everything else -> bright_value_v2) and, for a
// light that's currently off, sends switch_led=true in the same request. Untested on hardware.
static bool postCommands(int idx, JsonDocument& body, char* errOut, size_t errLen) {
    char path[64];
    snprintf(path, sizeof(path), "/v1.0/iot-03/devices/%s/commands", devices[idx].id);
    String payload;
    serializeJson(body, payload);
    JsonDocument respDoc;
    return tuyaCall("POST", path, payload.c_str(), respDoc, errOut, errLen);
}

static bool sendBrightness(int idx, int pct, char* errOut, size_t errLen) {
    TuyaDevice& d = devices[idx];
    JsonDocument body;
    JsonArray cmds = body["commands"].to<JsonArray>();
    if (!d.on) {
        JsonObject sw = cmds.add<JsonObject>();
        sw["code"] = TUYA_SWITCH_CODE;
        sw["value"] = true;
    }
    JsonObject cmd = cmds.add<JsonObject>();
    int raw = pct * 10;
    if (d.colour_mode) {
        cmd["code"] = "colour_data_v2";
        JsonObject v = cmd["value"].to<JsonObject>();
        v["h"] = d.hue;
        v["s"] = d.sat;
        v["v"] = raw;
    } else {
        cmd["code"] = "bright_value_v2";
        cmd["value"] = raw;
    }
    if (!postCommands(idx, body, errOut, errLen)) return false;

    d.on = true;
    d.bright_pct = pct;
    return true;
}

// Picks a palette colour (hue 0..360 at full saturation, brightness = the slider's current
// value) or white (hue < 0). work_mode is sent explicitly with it: the docs don't say whether
// colour_data_v2 alone switches a white-mode bulb over, so this doesn't rely on it doing so.
// Going to white also resends the slider's value as bright_value_v2, since in colour mode
// brightness lived in colour_data_v2's v and white mode keeps its own separate value.
static bool sendColour(int idx, int hue, int pct, char* errOut, size_t errLen) {
    TuyaDevice& d = devices[idx];
    JsonDocument body;
    JsonArray cmds = body["commands"].to<JsonArray>();
    if (!d.on) {
        JsonObject sw = cmds.add<JsonObject>();
        sw["code"] = TUYA_SWITCH_CODE;
        sw["value"] = true;
    }
    JsonObject mode = cmds.add<JsonObject>();
    mode["code"] = "work_mode";
    mode["value"] = hue < 0 ? "white" : "colour";
    JsonObject cmd = cmds.add<JsonObject>();
    if (hue < 0) {
        cmd["code"] = "bright_value_v2";
        cmd["value"] = pct * 10;
    } else {
        cmd["code"] = "colour_data_v2";
        JsonObject v = cmd["value"].to<JsonObject>();
        v["h"] = hue;
        v["s"] = 1000;
        v["v"] = pct * 10;
    }
    if (!postCommands(idx, body, errOut, errLen)) return false;

    d.on = true;
    d.colour_mode = hue >= 0;
    if (hue >= 0) {
        d.hue = hue;
        d.sat = 1000;
    }
    d.bright_pct = pct;
    return true;
}

static void fetchTask(void*) {
    fetch_error[0] = '\0';

    if (current_op == OP_AUTH) {
        ensureToken(fetch_error, sizeof(fetch_error));
    } else if (current_op == OP_LIST) {
        fetchDeviceList(fetch_error, sizeof(fetch_error));
    } else if (current_op == OP_TOGGLE && toggle_index >= 0 && toggle_index < device_count) {
        sendToggle(toggle_index, toggle_target, fetch_error, sizeof(fetch_error));
    } else if (current_op == OP_BRIGHTNESS && bright_index >= 0 && bright_index < device_count) {
        sendBrightness(bright_index, bright_pct_target, fetch_error, sizeof(fetch_error));
    } else if (current_op == OP_COLOUR && colour_index >= 0 && colour_index < device_count) {
        sendColour(colour_index, colour_hue_target, colour_pct_target, fetch_error, sizeof(fetch_error));
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

void setupTuyaLights(lv_obj_t* parent, lv_obj_t* brightnessParent) {
    lights_screen = parent;
    brightness_screen = brightnessParent;
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    // Plain flex container instead of a list: big buttons, centered, wrapping to a new row if
    // there isn't room (works whether there are 1, 2, or up to TUYA_MAX_DEVICES of them).
    cont_lights = lv_obj_create(parent);
    lv_obj_set_size(cont_lights, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cont_lights, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(cont_lights, 0, 0);
    // Device names can have accented letters or smart quotes the stock font doesn't have a
    // glyph for -- see CustomFonts.h. The icon label below overrides this with ext_32 anyway.
    lv_obj_set_style_text_font(cont_lights, &lv_font_montserrat_ext_14, 0);
    lv_obj_set_style_pad_all(cont_lights, 12, 0);
    lv_obj_set_style_pad_column(cont_lights, 16, 0);
    lv_obj_set_style_pad_row(cont_lights, 16, 0);
    lv_obj_remove_flag(cont_lights, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_lights, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont_lights, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Brightness page: Back, the light's name and a big readout on one row, a thin slider (its
    // knob is padded out so it's still easy to grab on the resistive panel), and the palette.
    lv_obj_t* btn_back = lv_button_create(brightness_screen);
    lv_obj_set_size(btn_back, 56, 36);
    lv_obj_set_pos(btn_back, 8, 8);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x222222), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btn_back, bright_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_icon = lv_label_create(btn_back);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_ext_18, 0);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_center(back_icon);

    label_bright_name = lv_label_create(brightness_screen);
    lv_label_set_long_mode(label_bright_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label_bright_name, 148);
    lv_obj_set_pos(label_bright_name, 72, 14);
    lv_obj_set_style_text_font(label_bright_name, &lv_font_montserrat_ext_18, 0);
    lv_obj_set_style_text_color(label_bright_name, lv_color_hex(0xFFFFFF), 0);

    label_bright_pct = lv_label_create(brightness_screen);
    lv_obj_set_style_text_font(label_bright_pct, &lv_font_montserrat_ext_32, 0);
    lv_obj_set_style_text_color(label_bright_pct, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label_bright_pct, LV_ALIGN_TOP_RIGHT, -10, 6);

    slider_bright = lv_slider_create(brightness_screen);
    lv_obj_set_size(slider_bright, 272, 8);
    lv_obj_align(slider_bright, LV_ALIGN_TOP_MID, 0, 62);
    lv_slider_set_range(slider_bright, 1, 100);
    lv_obj_set_style_bg_color(slider_bright, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_bright, lv_color_hex(COLOR_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_bright, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_bright, bright_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider_bright, bright_slider_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider_bright, bright_slider_cb, LV_EVENT_PRESS_LOST, NULL);

    // Two rows of six 36px round swatches, 12px apart, centered.
    for (int i = 0; i < PALETTE_COUNT; i++) {
        lv_obj_t* sw = lv_button_create(brightness_screen);
        lv_obj_set_size(sw, 36, 36);
        lv_obj_set_pos(sw, 22 + (i % 6) * 48, 90 + (i / 6) * 44);
        lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
        lv_color_t col = hueColor(palette_hues[i]);
        lv_obj_set_style_bg_color(sw, col, 0);
        lv_obj_set_style_bg_color(sw, col, LV_STATE_PRESSED);  // no darkening: it would happen in the pre-swapped space
        lv_obj_set_style_shadow_width(sw, 0, 0);
        lv_obj_set_style_border_color(sw, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(sw, 0, 0);
        lv_obj_add_event_cb(sw, swatch_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        swatches[i] = sw;
    }

    label_bright_status = lv_label_create(brightness_screen);
    lv_label_set_long_mode(label_bright_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label_bright_status, 300);
    lv_obj_set_style_text_align(label_bright_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label_bright_status, &lv_font_montserrat_ext_14, 0);
    lv_obj_set_style_text_color(label_bright_status, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(label_bright_status, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(label_bright_status, "");
}

void startTuyaAuth() {
    if (fetching) return;
    current_op = OP_AUTH;
    startTask();
}

void refreshTuyaLights() {
    if (returning_from_brightness) {
        // Back from the brightness page: what's in devices[] is current (including whatever was
        // just changed there), so redraw it instead of paying for a reload.
        returning_from_brightness = false;
        showList();
        return;
    }
    pending_index = -1;  // a fresh full reload supersedes any toggle/brightness/colour queued from before
    pending_bright_index = -1;
    pending_colour_index = -1;
    if (fetching) {
        // Something's already in flight -- almost certainly the boot-time pre-auth. Don't drop
        // this: run it the moment that finishes instead (see loopTuyaLights()).
        pending_list_refresh = true;
        setStatus(LV_SYMBOL_REFRESH " Loading...");
        return;
    }
    current_op = OP_LIST;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startTask();
}

void loopTuyaLights() {
    if (!fetch_done) return;
    fetch_done = false;
    fetching = false;

    // OP_AUTH never touches devices[]/the screen -- nothing to render, whether it succeeded or
    // not (a failure here just means the next real call pays for authenticating itself).
    if (current_op == OP_BRIGHTNESS || current_op == OP_COLOUR) {
        // Reported on the brightness page, not the list. On failure the slider and palette snap
        // back to what the light is actually at. The list only needs a redraw if it's what's on
        // screen (the command may also have switched the light on).
        if (fetch_error[0]) {
            char msg[110];
            snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s", fetch_error);
            lv_label_set_text(label_bright_status, msg);
            if (brightness_dev >= 0 && brightness_dev < device_count) {
                lv_slider_set_value(slider_bright, devices[brightness_dev].bright_pct, LV_ANIM_OFF);
                lv_label_set_text_fmt(label_bright_pct, "%d%%", devices[brightness_dev].bright_pct);
                updateSwatchSelection(brightness_dev);
            }
        } else {
            lv_label_set_text(label_bright_status, "");
        }
        if (!lv_obj_has_flag(lights_screen, LV_OBJ_FLAG_HIDDEN)) showList();
    } else if (current_op != OP_AUTH) {
        if (fetch_error[0]) showError(fetch_error);
        else showList();
    }

    if (pending_list_refresh) {
        pending_list_refresh = false;
        current_op = OP_LIST;
        setStatus(LV_SYMBOL_REFRESH " Loading...");
        startTask();
    } else if (pending_index >= 0) {
        int idx = pending_index;
        pending_index = -1;
        if (idx < device_count) startToggle(idx);
    } else if (pending_bright_index >= 0) {
        int idx = pending_bright_index;
        pending_bright_index = -1;
        if (idx < device_count) startBrightness(idx, pending_bright_pct);
    } else if (pending_colour_index >= 0) {
        int idx = pending_colour_index;
        pending_colour_index = -1;
        if (idx < device_count) startColour(idx, pending_colour_hue, pending_colour_pct);
    }
}
