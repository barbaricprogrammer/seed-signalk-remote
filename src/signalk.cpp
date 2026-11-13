// Minimal Signal K delta client: WiFi + hand-rolled WebSocket (RFC6455, text frames).
#include "alarms.h"
#include "daynight.h"
#include <rpcWiFi.h>
#include <ArduinoJson.h>
#include "config.h"
#include "state.h"
#include "signalk.h"
#include <FlashStorage_SAMD.h>

// ---- access token persistence (SAMD51 emulated EEPROM in flash) ----
struct Stored { uint32_t magic; char token[640]; };
static const uint32_t MAGIC = 0x57494F31;   // "WIO1"
static char token[640] = "";
static char reqHref[80] = "";
static uint32_t authMs = 0;
static bool requesting = false;

static void loadToken() {
  Stored st; EEPROM.get(0, st);
  if (st.magic == MAGIC && strlen(st.token) > 20) { strlcpy(token, st.token, sizeof token); strlcpy(S.auth, "token stored", sizeof S.auth); }
}
static void saveToken(const char* t) {
  Stored st; memset(&st, 0, sizeof st); st.magic = t ? MAGIC : 0; if (t) strlcpy(st.token, t, sizeof st.token);
  EEPROM.put(0, st); EEPROM.commit();
  strlcpy(token, t ? t : "", sizeof token);
}
static void makeClientId() {
  volatile uint32_t* w[4] = {(uint32_t*)0x008061FC, (uint32_t*)0x00806010, (uint32_t*)0x00806014, (uint32_t*)0x00806018};
  snprintf(S.clientId, sizeof S.clientId, "wio-terminal-%08x", (unsigned)(*w[0] ^ *w[1] ^ *w[2] ^ *w[3]));
}

// Print::printf on this core may truncate long output, so format into our own buffer and write once.
static void sendf(WiFiClient& c, const char* fmt, ...) {
  static char out[1100];
  va_list ap; va_start(ap, fmt); int n = vsnprintf(out, sizeof out, fmt, ap); va_end(ap);
  if (n > 0) c.write((const uint8_t*)out, min((size_t)n, sizeof out - 1));
}

// minimal HTTP/1.1 request; returns status code, JSON body parsed into doc
static int http(const char* method, const char* path, const char* body, JsonDocument& doc) {
  WiFiClient c; if (!c.connect(SK_HOST, SK_PORT, 3000)) return -1;
  sendf(c, "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n", method, path, SK_HOST, SK_PORT);
  if (token[0]) sendf(c, "Authorization: JWT %s\r\n", token);
  if (body) sendf(c, "Content-Type: application/json\r\nContent-Length: %u\r\n\r\n%s", (unsigned)strlen(body), body); else c.print("\r\n");
  String r; uint32_t t = millis();
  while ((c.connected() || c.available()) && millis() - t < 4000 && r.length() < 3000) { while (c.available()) r += (char)c.read(); delay(2); }
  c.stop();
  int code = r.startsWith("HTTP/") ? r.substring(9, 12).toInt() : -2;
  int a = r.indexOf('{'), b = r.lastIndexOf('}');
  if (a >= 0 && b > a) deserializeJson(doc, r.c_str() + a, b - a + 1);
  return code;
}

// Signal K device access request: POST /signalk/v1/access/requests, then poll the returned href.
static void authStep() {
  uint32_t now = millis();
  if (now - authMs < (requesting ? 3000 : 20000) && authMs) return;
  authMs = now;
  JsonDocument d;
  if (!requesting) {
    char body[200];
    snprintf(body, sizeof body, "{\"clientId\":\"%s\",\"description\":\"Wio Terminal Signal K remote\",\"permissions\":\"readwrite\"}", S.clientId);
    int code = http("POST", "/signalk/v1/access/requests", body, d);
    if (code == 202 || code == 200) {
      strlcpy(reqHref, d["href"] | "", sizeof reqHref);
      if (reqHref[0]) { requesting = true; strlcpy(S.auth, "PENDING approval", sizeof S.auth); }
    } else { snprintf(S.auth, sizeof S.auth, "request http %d", code); }
    return;
  }
  int code = http("GET", reqHref, nullptr, d);
  const char* state = d["state"] | "";
  if (code == 200 && !strcmp(state, "COMPLETED")) {
    const char* perm = d["accessRequest"]["permission"] | "";
    const char* tk = d["accessRequest"]["token"] | "";
    if (!strcmp(perm, "APPROVED") && tk[0]) { saveToken(tk); requesting = false; strlcpy(S.auth, "approved", sizeof S.auth); }
    else { requesting = false; strlcpy(S.auth, "DENIED (retrying)", sizeof S.auth); }
  } else if (code == 404) { requesting = false; strlcpy(S.auth, "request expired", sizeof S.auth); }
}

State S;
static WiFiClient cli;
static bool upgraded = false;
static uint32_t wsUp = 0, rxBytes = 0;
static uint32_t lastTry = 0, lastWifiTry = 0, rateMs = 0, rateBase = 0;
static const size_t BUF = 4096;
static uint8_t buf[BUF];
static size_t have = 0;

static const char SUB[] =
  "{\"context\":\"vessels.self\",\"sourcePolicy\":\"all\",\"subscribe\":["
  "{\"path\":\"navigation.position\",\"period\":1000},"
  "{\"path\":\"navigation.datetime\",\"period\":10000},"
  "{\"path\":\"environment.sunlight.times.*\",\"period\":30000},"
  "{\"path\":\"navigation.speedOverGround\",\"period\":1000},"
  "{\"path\":\"navigation.courseOverGroundTrue\",\"period\":1000},"
  "{\"path\":\"navigation.headingTrue\",\"period\":1000},"
  "{\"path\":\"navigation.headingMagnetic\",\"period\":1000},"
  "{\"path\":\"navigation.magneticVariation\",\"period\":5000},"
  "{\"path\":\"navigation.attitude\",\"period\":500},"
  "{\"path\":\"navigation.gnss.satellitesInView\",\"policy\":\"instant\"},"
  "{\"path\":\"navigation.gnss.satellites\",\"period\":2000},"
  "{\"path\":\"navigation.gnss.horizontalDilution\",\"period\":2000},"
  "{\"path\":\"environment.*\",\"period\":3000},"
  "{\"path\":\"notifications.*\",\"policy\":\"instant\"}"
  "]}";

static void err(const char* m) { strlcpy(S.lastErr, m, sizeof S.lastErr); }

static void wsSend(const char* txt) {
  size_t n = strlen(txt);
  uint8_t hdr[8]; int h = 0;
  hdr[h++] = 0x81;
  uint8_t mask[4] = {(uint8_t)random(256), (uint8_t)random(256), (uint8_t)random(256), (uint8_t)random(256)};
  if (n < 126) hdr[h++] = 0x80 | n; else { hdr[h++] = 0x80 | 126; hdr[h++] = n >> 8; hdr[h++] = n & 0xff; }
  memcpy(hdr + h, mask, 4); h += 4;
  static uint8_t out[1100];
  if (n > sizeof out) return;
  for (size_t i = 0; i < n; i++) out[i] = txt[i] ^ mask[i & 3];
  cli.write(hdr, h); cli.write(out, n);
}

bool skPublish(const char* path, float value) {
  if (!upgraded || !cli.connected()) return false;
  char b[200];
  snprintf(b, sizeof b, "{\"context\":\"vessels.self\",\"updates\":[{\"values\":[{\"path\":\"%s\",\"value\":%.4f}]}]}", path, value);
  wsSend(b); return true;
}

static void wsPong(const uint8_t* p, size_t n) {
  uint8_t hdr[6] = {0x8A, (uint8_t)(0x80 | n), 0, 0, 0, 0};   // mask key zero -> payload unchanged
  cli.write(hdr, 6); if (n) cli.write(p, n);
}

static SatSource* srcFor(const char* name) {
  for (int i = 0; i < S.nSrc; i++) if (!strcmp(S.src[i].name, name)) return &S.src[i];
  if (S.nSrc >= 6) return nullptr;
  SatSource* s = &S.src[S.nSrc++]; strlcpy(s->name, name, sizeof s->name); return s;
}

static void handleValue(const char* srcName, const char* path, JsonVariantConst v) {
  if (!strncmp(path, "notifications.", 14)) { alarmUpdate(path, v); return; }
  if (!strncmp(path, "environment.sunlight.times.", 27)) { if (v.is<const char*>()) dnTime(path + 27, v.as<const char*>()); return; }
  if (!strcmp(path, "navigation.datetime")) { if (v.is<const char*>()) dnDatetime(v.as<const char*>()); return; }
  auto num = [&](Num& n) { if (v.is<float>() || v.is<int>()) n.set(v.as<float>()); };
  // any source reporting position/GNSS data gets a tab, even if it sends no satellite sentences
  if (!strncmp(path, "navigation.position", 19) || !strncmp(path, "navigation.gnss.", 16)) srcFor(srcName);
  if (!strcmp(path, "navigation.position")) {
    if (v.isNull()) { S.fix = false; return; }
    S.lat = v["latitude"] | 0.0; S.lon = v["longitude"] | 0.0; S.fix = true; S.posMs = millis();
    strlcpy(S.fixSrc, srcName, sizeof S.fixSrc);
  }
  else if (!strcmp(path, "navigation.speedOverGround")) num(S.sog);
  else if (!strcmp(path, "navigation.courseOverGroundTrue")) num(S.cog);
  else if (!strcmp(path, "navigation.headingTrue")) num(S.hdg);
  else if (!strcmp(path, "navigation.headingMagnetic")) num(S.hdgMag);
  else if (!strcmp(path, "navigation.magneticVariation")) num(S.magVar);   // radians, east positive
  else if (!strcmp(path, "navigation.attitude")) {
    if (v["roll"].is<float>()) S.roll.set(v["roll"]);
    if (v["pitch"].is<float>()) S.pitch.set(v["pitch"]);
  }
  else if (!strcmp(path, "navigation.gnss.satellites")) num(S.satsUsed);
  else if (!strcmp(path, "navigation.gnss.horizontalDilution")) num(S.hdop);
  else if (!strcmp(path, "navigation.gnss.satellitesInView")) {
    SatSource* s = srcFor(srcName); if (!s) return;
    strlcpy(s->gnss, v["gnss"] | "", sizeof s->gnss);
    s->count = 0; s->lastMs = millis();
    for (JsonVariantConst e : v["satellites"].as<JsonArrayConst>()) {
      if (s->count >= 24) break;
      Sat& t = s->sats[s->count++];
      t.id = e["id"] | 0; t.snr = e["SNR"] | 0;
      t.el = (int)lroundf((e["elevation"] | 0.0f) * 57.29578f);   // Signal K sends radians; store degrees
      t.az = (int)lroundf((e["azimuth"] | 0.0f) * 57.29578f);
    }
  }
  else if (!strcmp(path, "environment.outside.temperature")) num(S.airTemp);
  else if (!strcmp(path, "environment.water.temperature")) num(S.waterTemp);
  else if (!strcmp(path, "environment.outside.pressure")) num(S.pressure);
  else if (!strcmp(path, "environment.outside.humidity") || !strcmp(path, "environment.outside.relativeHumidity")) num(S.humidity);
  else if (!strcmp(path, "environment.depth.belowTransducer")) num(S.depth);
  else if (!strcmp(path, "environment.wind.speedApparent")) num(S.awsp);
  else if (!strcmp(path, "environment.wind.angleApparent")) num(S.awa);
  else if (!strcmp(path, "environment.wind.speedTrue")) num(S.twsp);
  else if (!strcmp(path, "environment.wind.angleTrueWater") || !strcmp(path, "environment.wind.angleTrueGround")) num(S.twa);
  else if (!strcmp(path, "environment.rpi.cpu.temperature")) num(S.piCpuTemp);
  else if (!strcmp(path, "environment.rpi.cpu.utilisation")) num(S.piCpuUtil);
  else if (!strcmp(path, "environment.rpi.cpu.load.1m")) num(S.piLoad1);
  else if (!strcmp(path, "environment.rpi.cpu.coreVoltage")) num(S.piVolt);
  else if (!strcmp(path, "environment.rpi.memory.utilisation")) num(S.piMem);
  else if (!strcmp(path, "environment.rpi.storage.utilisation")) num(S.piDisk);
  else if (!strcmp(path, "environment.rpi.uptime")) num(S.piUptime);
  else if (!strcmp(path, "environment.rpi.throttling.throttled")) S.piThrottled = v | false;
  else if (!strcmp(path, "environment.rpi.throttling.underVoltage")) S.piUnderV = v | false;
}

static void handleMessage(const char* txt, size_t n) {
  JsonDocument filter;
  filter["updates"][0]["$source"] = true;
  filter["updates"][0]["values"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, txt, n, DeserializationOption::Filter(filter))) { S.dropped++; return; }
  S.msgCount++; S.lastMsgMs = millis();
  for (JsonObjectConst u : doc["updates"].as<JsonArrayConst>()) {
    const char* src = u["$source"] | "?";
    for (JsonObjectConst val : u["values"].as<JsonArrayConst>())
      handleValue(src, val["path"] | "", val["value"]);
  }
}

static void closeWs(const char* why) {
  cli.stop(); upgraded = false; have = 0; S.ws = false; err(why); lastTry = millis();
}

static bool connectWs() {
  if (!cli.connect(SK_HOST, SK_PORT, 3000)) { err("tcp connect failed"); return false; }
  char key[25]; static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i = 0; i < 22; i++) key[i] = b64[random(64)]; key[22] = '='; key[23] = '='; key[24] = 0;
  sendf(cli, "GET /signalk/v1/stream?subscribe=none HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
             "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n", SK_HOST, SK_PORT, key);
  if (token[0]) sendf(cli, "Authorization: JWT %s\r\n", token);
  cli.print("\r\n");
  uint32_t t = millis(); String resp;
  while (millis() - t < 3000 && !resp.endsWith("\r\n\r\n")) { while (cli.available()) resp += (char)cli.read(); delay(2); }
  if (resp.indexOf(" 401 ") > 0 || resp.indexOf(" 403 ") > 0) { cli.stop(); saveToken(nullptr); S.authed = false; strlcpy(S.auth, "token rejected", sizeof S.auth); err("auth rejected"); return false; }
  if (resp.indexOf(" 101 ") < 0) { cli.stop(); err("ws upgrade refused"); return false; }
  upgraded = true; have = 0; S.ws = true; S.reconnects++; S.lastMsgMs = 0; wsUp = millis(); rxBytes = 0;
  wsSend(SUB);
  return true;
}

static void pump() {
  int a = cli.available();
  if (a > 0 && have < BUF) { int r = cli.read(buf + have, min((size_t)a, BUF - have)); if (r > 0) { have += r; rxBytes += r; } }
  while (have >= 2) {
    uint8_t op = buf[0] & 0x0F; size_t len = buf[1] & 0x7F, h = 2;
    if (len == 126) { if (have < 4) return; len = (buf[2] << 8) | buf[3]; h = 4; }
    else if (len == 127) { closeWs("frame too large"); return; }
    if (len + h > BUF) { // oversize: discard as it streams in
      closeWs("oversize frame"); S.dropped++; return;
    }
    if (have < h + len) return;
    if (op == 0x1) handleMessage((const char*)buf + h, len);
    else if (op == 0x9) wsPong(buf + h, len);
    else if (op == 0x8) { closeWs("server closed"); return; }
    memmove(buf, buf + h + len, have - h - len); have -= h + len;
  }
}

void skBegin() {
  makeClientId(); loadToken();
  uint32_t t = millis(); while (!Serial && millis() - t < 2500) delay(10);
  Serial.println("boot: wifi init");
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(200);
  int n = WiFi.scanNetworks(); Serial.printf("scan: %d networks\n", n);
  for (int i = 0; i < n; i++) Serial.printf("  %-24s ch%d %ddBm enc%d\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i));
  Serial.printf("rtl8720 fw: %s\n", rpc_system_version());
  int r = WiFi.begin(WIFI_SSID, WIFI_PASS); lastWifiTry = millis();
  Serial.printf("begin() -> %d status=%d\n", r, WiFi.status());
}

void skLoop() {
  uint32_t now = millis();
  S.wifi = WiFi.status() == WL_CONNECTED;
  static uint32_t lastLog = 0;
  if (now - lastLog > 3000) { lastLog = now; S.wifiStatus = WiFi.status(); Serial.printf("wifi=%d ws=%d auth=%s err=%s msgs=%u drop=%u rx=%u have=%u\n", S.wifiStatus, S.ws, S.auth, S.lastErr, (unsigned)S.msgCount, (unsigned)S.dropped, (unsigned)rxBytes, (unsigned)have); }
  if (!S.wifi) {
    if (S.ws) closeWs("wifi lost");
    if (now - lastWifiTry > 30000) { int r = WiFi.begin(WIFI_SSID, WIFI_PASS); Serial.printf("retry begin() -> %d\n", r); lastWifiTry = millis(); }
    return;
  }
  S.authed = token[0];
  if (!token[0]) { authStep(); return; }
  if (!upgraded) { if (now - lastTry > 3000) { lastTry = now; connectWs(); } return; }
  if (!cli.connected()) { closeWs("disconnected"); return; }
  pump();
  if ((int32_t)(millis() - (S.lastMsgMs ? S.lastMsgMs : wsUp)) > 20000) closeWs("stale (20s)");
  if (now - rateMs >= 1000) { S.msgRate = S.msgCount - rateBase; rateBase = S.msgCount; rateMs = now; }
}
