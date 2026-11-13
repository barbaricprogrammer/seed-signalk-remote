// Fetches a pre-drawn 4-bit chart image (depth/land) from tile-relay and blits it into the display sprite.
#include <rpcWiFi.h>
#include <math.h>
#include "config.h"
#include "state.h"
#include "relay.h"
#include "chartpal.h"

static const int IW = 320, IH = 198, IBYTES = IW * IH / 2;
static uint8_t img[IBYTES];
static bool valid = false;
static double fLat, fLon; static float fMpp;
static uint32_t lastTry = 0, failUntil = 0, lastOkMs = 0, lastDurMs = 0, nOk = 0, nFail = 0;
static char status[28] = "off";
static uint8_t pal332[16];

bool relayEnabled() { return RELAY_HOST[0] != 0; }
const char* relayStatus() { return status; }

static bool fetch(double lat, double lon, float mpp) {
  WiFiClient c;
  uint32_t t0 = millis();
  if (!c.connect(RELAY_HOST, RELAY_PORT, 2000)) { strlcpy(status, "connect failed", sizeof status); return false; }
  char req[200];
  int n = snprintf(req, sizeof req, "GET /chart?lat=%.6f&lon=%.6f&mpp=%.3f&w=%d&h=%d HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                   lat, lon, mpp, IW, IH, RELAY_HOST);
  c.write((const uint8_t*)req, n);
  // response headers
  char first[16] = ""; int fi = 0; uint32_t last4 = 0; bool hdrDone = false;
  while (!hdrDone && millis() - t0 < 6000) {
    if (!c.available()) { if (!c.connected()) break; delay(2); continue; }
    uint8_t b = c.read();
    if (fi < 15 && b != '\r' && b != '\n' && !(fi == 0 && false)) first[fi++] = b; else if (b == '\n' && fi < 15) fi = 15;
    last4 = (last4 << 8) | b;
    if (last4 == 0x0D0A0D0A) hdrDone = true;
  }
  if (!hdrDone || strncmp(first, "HTTP/1.1 200", 12)) { snprintf(status, sizeof status, "http: %.12s", first); c.stop(); return false; }
  int got = 0;
  while (got < IBYTES && millis() - t0 < 9000) {
    int a = c.available();
    if (a <= 0) { if (!c.connected()) break; delay(2); continue; }
    int r = c.read(img + got, min(a, IBYTES - got)); if (r > 0) got += r;
  }
  c.stop();
  if (got != IBYTES) { snprintf(status, sizeof status, "short %d/%d", got, IBYTES); return false; }
  fLat = lat; fLon = lon; fMpp = mpp; valid = true; lastOkMs = millis(); lastDurMs = lastOkMs - t0; nOk++;
  snprintf(status, sizeof status, "ok %ums", (unsigned)lastDurMs);
  return true;
}

void relayUpdate(double lat, double lon, float mpp) {
  if (!relayEnabled() || !S.wifi) return;
  uint32_t now = millis();
  if (pal332[0] == 0 && pal332[1] == 0) for (int i = 0; i < 16; i++) { uint16_t c = CHART_PAL[i]; pal332[i] = ((c & 0xE000) >> 8) | ((c & 0x0700) >> 6) | ((c & 0x0018) >> 3); }
  bool stale = !valid || fabsf(mpp - fMpp) > 0.01f * mpp;
  if (!stale) {
    float kx = 111320.0f * cosf(lat * DEG_TO_RAD);
    float dx = fabs((lon - fLon) * kx / mpp), dy = fabs((lat - fLat) * 111320.0 / mpp);
    stale = dx > 30 || dy > 30;
  }
  if (!stale || now < failUntil || now - lastTry < 1500) return;
  lastTry = now;
  if (!fetch(lat, lon, mpp)) { nFail++; failUntil = millis() + 8000; }
}

bool relayBlit(uint8_t* buf, int W, int H, int centreY, double lat, double lon, float mpp) {
  if (!valid || !buf || fabsf(mpp - fMpp) > 0.01f * mpp) return false;
  float kx = 111320.0f * cosf(lat * DEG_TO_RAD);
  int offX = (int)lroundf((fLon - lon) * kx / mpp);
  int offY = (int)lroundf((lat - fLat) * 111320.0 / mpp);
  int y0 = centreY - IH / 2 + offY, x0 = W / 2 - IW / 2 + offX;   // image top-left on screen
  const uint8_t water = pal332[0];
  for (int y = 0; y < H; y++) {
    uint8_t* row = buf + y * W; int iy = y - y0;
    if (iy < 0 || iy >= IH) { for (int x = 0; x < W; x++) row[x] = water; continue; }
    const uint8_t* src = img + iy * (IW / 2);
    for (int x = 0; x < W; x++) {
      int ix = x - x0;
      if (ix < 0 || ix >= IW) { row[x] = water; continue; }
      uint8_t b = src[ix >> 1];
      row[x] = pal332[(ix & 1) ? (b & 0x0F) : (b >> 4)];
    }
  }
  return true;
}
