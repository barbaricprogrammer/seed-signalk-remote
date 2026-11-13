#include "alarms.h"
#include "daynight.h"
#include <rpcWiFi.h>
#include "config.h"
#include "state.h"
#include "views.h"
#include "relay.h"

static TFT_eSPI tft;
static TFT_eSprite spr(&tft);
static const int W = 320, H = 240, TOP = 20;

// 8-bit-sprite friendly colours (RGB565 values)
#define C_BG    TFT_BLACK
#define C_FG    TFT_WHITE
#define C_DIM   0x7BEF
#define C_GRID  0x2124
#define C_ACC   TFT_CYAN
#define C_OK    TFT_GREEN
#define C_BAD   TFT_RED
#define C_WARN  TFT_YELLOW

static int page[V_COUNT];
static const char* NAMES[V_COUNT] = {"CHART", "GPS", "ATTITUDE", "DEBUG"};

// ---------- chart state ----------
static const float ZOOM_NM[] = {0.25f, 0.5f, 1, 2, 5};   // half-height of view in nm
static int zoom = 2;
static float panN = 0, panE = 0;       // metres offset from centre target
static bool follow = true;
static double cLat = DEFAULT_LAT, cLon = DEFAULT_LON;
struct Pt { float lat, lon; };
static Pt trail[90]; static int trailN = 0, trailHead = 0; static uint32_t trailMs = 0;

// ---------- helpers ----------
static void fmtDM(char* out, size_t n, double v, bool isLat) {
  char h = isLat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W'); v = fabs(v);
  int d = (int)v; double m = (v - d) * 60;
  snprintf(out, n, isLat ? "%02d %06.3f%c" : "%03d %06.3f%c", d, m, h);
}
static void txt(int x, int y, const char* s, uint16_t fg, int size = 1, int datum = TL_DATUM) {
  spr.setTextDatum(datum); spr.setTextColor(fg); spr.setTextSize(size); spr.drawString(s, x, y);
}
static void header(int view) {
  spr.fillRect(0, 0, W, TOP, 0x0010);
  char b[40];
  snprintf(b, sizeof b, "%c %s", 'A' + view, NAMES[view]);
  txt(4, 3, b, C_ACC, 2);
  // pager dots
  static const int pages[V_COUNT] = {0, 0, 2, 2};
  if (pages[view]) { snprintf(b, sizeof b, "p%d/%d", page[view] + 1, pages[view]); txt(150, 6, b, C_DIM); }
  // link indicators
  txt(W - 4, 6, S.ws ? "SK" : (S.wifi ? "wifi" : "---"), S.ws ? C_OK : (S.wifi ? C_WARN : C_BAD), 1, TR_DATUM);
  spr.fillCircle(W - 30, 10, 4, S.fix ? C_OK : C_BAD);
}
static const char* nz(const Num& n, const char* f, float scale, char* b, size_t sz, float off = 0) {
  if (!n.ok()) strlcpy(b, "--", sz); else snprintf(b, sz, f, n.v * scale + off);
  return b;
}
static const float RAD2DEG = 57.29578f, MS2KN = 1.94384f;

static void bar(int x, int y, int w, int h, float frac, uint16_t col) {
  spr.drawRect(x, y, w, h, C_DIM);
  frac = constrain(frac, 0.f, 1.f);
  spr.fillRect(x + 1, y + 1, (int)((w - 2) * frac), h - 2, col);
}

// ================= V_CHART =================
static void mToLL(double lat0, double lon0, float dn, float de, double& lat, double& lon) {
  lat = lat0 + dn / 111320.0;
  lon = lon0 + de / (111320.0 * cos(lat0 * DEG_TO_RAD));
}
static void drawChart() {
  uint32_t now = millis();
  bool haveFix = S.fix;
  if (haveFix && now - trailMs > 4000) {
    trailMs = now; trail[trailHead] = {(float)S.lat, (float)S.lon};
    trailHead = (trailHead + 1) % 90; if (trailN < 90) trailN++;
  }
  if (follow && haveFix) { cLat = S.lat; cLon = S.lon; panN = panE = 0; }
  double lat0, lon0; mToLL(cLat, cLon, panN, panE, lat0, lon0);

  const int ch = H - TOP;                             // chart pixel height
  float mpp = ZOOM_NM[zoom] * 1852.f / (ch / 2.f);    // metres per pixel
  auto toPx = [&](double lat, double lon, int& x, int& y) {
    float n = (lat - lat0) * 111320.0, e = (lon - lon0) * 111320.0 * cos(lat0 * DEG_TO_RAD);
    x = W / 2 + (int)(e / mpp); y = TOP + ch / 2 - (int)(n / mpp);
  };
  relayUpdate(lat0, lon0, mpp);
  bool tiles = relayBlit((uint8_t*)spr.getPointer(), W, H, TOP + ch / 2, lat0, lon0, mpp);
  if (!tiles) spr.fillRect(0, TOP, W, ch, 0x0008);    // "water"

  // lat/lon grid, step chosen so lines are >= ~50px apart
  static const double steps[] = {1.0/60/20, 1.0/60/10, 1.0/60/4, 1.0/60/2, 1.0/60, 2.0/60, 5.0/60, 10.0/60, 0.5, 1, 5};
  double step = steps[10]; for (double s : steps) if (s * 111320.0 / mpp >= 50) { step = s; break; }
  char b[24];
  double latMin = lat0 - (ch / 2 * mpp) / 111320.0, latMax = lat0 + (ch / 2 * mpp) / 111320.0;
  double dLon = (W / 2 * mpp) / (111320.0 * cos(lat0 * DEG_TO_RAD));
  if (!tiles) for (double la = ceil(latMin / step) * step; la < latMax; la += step) {
    int x, y; toPx(la, lon0, x, y); spr.drawFastHLine(0, y, W, C_GRID);
    fmtDM(b, sizeof b, la, true); txt(2, y - 9, b, C_DIM);
  }
  if (!tiles) for (double lo = ceil((lon0 - dLon) / step) * step; lo < lon0 + dLon; lo += step) {
    int x, y; toPx(lat0, lo, x, y); spr.drawFastVLine(x, TOP, ch, C_GRID);
    fmtDM(b, sizeof b, lo, false); txt(x + 2, H - 10, b, C_DIM);
  }
  // trail
  int px = -1, py = -1;
  for (int i = 0; i < trailN; i++) {
    Pt& p = trail[(trailHead - trailN + i + 90) % 90]; int x, y; toPx(p.lat, p.lon, x, y);
    if (px >= 0) spr.drawLine(px, py, x, y, C_WARN);
    px = x; py = y;
  }
  // vessel: boat icon (heading preferred, else COG); dim ghost at the centre when there is no fix
  {
    int x, y;
    if (haveFix) toPx(S.lat, S.lon, x, y); else { x = W / 2; y = TOP + ch / 2; }
    // true heading if sent; else compass heading + variation (once the server derives it); else raw compass; else COG
    float a = S.hdg.ok() ? S.hdg.v
            : S.hdgMag.ok() ? S.hdgMag.v + (S.magVar.ok(600000) ? S.magVar.v : 0)
            : (S.cog.ok() ? S.cog.v : 0);
    float s = sinf(a), c = cosf(a);
    auto rot = [&](float dx, float dy, int& ox, int& oy) { ox = x + (int)lroundf(dx * c + dy * s); oy = y + (int)lroundf(dx * s - dy * c); };
    int bx[5], by[5];   // bow, port shoulder, port stern, starboard stern, starboard shoulder
    rot(0, 14, bx[0], by[0]); rot(-6, 4, bx[1], by[1]); rot(-5, -11, bx[2], by[2]); rot(5, -11, bx[3], by[3]); rot(6, 4, bx[4], by[4]);
    uint16_t hull = haveFix ? C_FG : C_DIM, edge = haveFix ? C_BG : C_BAD;
    spr.fillTriangle(bx[0], by[0], bx[1], by[1], bx[4], by[4], hull);
    spr.fillTriangle(bx[1], by[1], bx[4], by[4], bx[2], by[2], hull);
    spr.fillTriangle(bx[4], by[4], bx[2], by[2], bx[3], by[3], hull);
    for (int i = 0; i < 5; i++) spr.drawLine(bx[i], by[i], bx[(i + 1) % 5], by[(i + 1) % 5], edge);
    int cx0, cy0, cx1, cy1; rot(0, 9, cx0, cy0); rot(0, -7, cx1, cy1);   // centreline
    spr.drawLine(cx0, cy0, cx1, cy1, haveFix ? C_BAD : edge);
    int m0, n0, m1, n1; rot(-3, -2, m0, n0); rot(3, -2, m1, n1);          // beam marker
    spr.drawLine(m0, n0, m1, n1, haveFix ? C_BAD : edge);
    if (haveFix && (x < 0 || x >= W || y < TOP || y >= H))
      txt(W / 2, TOP + 4, "vessel off-screen (press joystick)", C_WARN, 1, TC_DATUM);
    if (!haveFix) txt(W / 2, TOP + ch / 2 + 22, "NO GPS FIX", C_BAD, 2, TC_DATUM);
  }
  // crosshair when panning
  if (!follow) { spr.drawFastHLine(W / 2 - 6, TOP + ch / 2, 13, C_ACC); spr.drawFastVLine(W / 2, TOP + ch / 2 - 6, 13, C_ACC); }
  // info strip
  spr.fillRect(0, TOP, W, 22, 0x0010 /*dark*/); spr.drawFastHLine(0, TOP + 22, W, C_DIM);
  if (haveFix) {
    char la[20], lo[20]; fmtDM(la, sizeof la, S.lat, true); fmtDM(lo, sizeof lo, S.lon, false);
    snprintf(b, sizeof b, "%s %s", la, lo); txt(4, TOP + 2, b, C_FG);
  } else txt(4, TOP + 2, "-- no position --", C_DIM);
  char sg[12], cg[12]; nz(S.sog, "%.1f", MS2KN, sg, sizeof sg); nz(S.cog, "%.0f", RAD2DEG, cg, sizeof cg);
  snprintf(b, sizeof b, "SOG %skn COG %s", sg, cg); txt(4, TOP + 12, b, C_ACC);
  snprintf(b, sizeof b, "%.2fnm %s", ZOOM_NM[zoom], follow ? "FOLLOW" : "PAN"); txt(W - 4, TOP + 12, b, follow ? C_OK : C_WARN, 1, TR_DATUM);
  // scale bar (1/4 of the half-height)
  float nm = ZOOM_NM[zoom] / 2; int len = (int)(nm * 1852.f / mpp);
  uint16_t sc = tiles ? TFT_BLACK : C_FG;
  spr.drawFastHLine(W - 10 - len, H - 8, len, sc); spr.drawFastVLine(W - 10 - len, H - 11, 7, sc); spr.drawFastVLine(W - 10, H - 11, 7, sc);
  snprintf(b, sizeof b, "%.3gnm", nm); txt(W - 10 - len / 2, H - 20, b, sc, 1, TC_DATUM);
}

// ================= V_SATS =================
static int srcSel = 0;
// same scale as the Signal K admin UI: green >=40 dB, blue 30-40, red 0-30, black = no signal
static uint16_t snrCol(int snr) { return snr >= 40 ? TFT_GREEN : snr >= 30 ? 0x2D7F : snr > 0 ? TFT_RED : TFT_BLACK; }
static void drawSats() {
  char b[48];
  if (S.nSrc == 0) { txt(W / 2, 110, "waiting for satellite data...", C_DIM, 1, TC_DATUM); return; }
  if (srcSel >= S.nSrc) srcSel = 0;
  SatSource& s = S.src[srcSel];
  // source tabs: one per GPS/GNSS source, selected one highlighted; B cycles to the next
  int tw = min(80, W / S.nSrc);
  for (int i = 0; i < S.nSrc; i++) {
    const char* nm = strrchr(S.src[i].name, '.'); nm = nm ? nm + 1 : S.src[i].name;
    snprintf(b, sizeof b, "%s %d", nm, S.src[i].count);
    int x = i * tw; bool sel = i == srcSel;
    spr.fillRect(x + 1, TOP + 2, tw - 2, 16, sel ? C_ACC : 0x2104);
    txt(x + tw / 2, TOP + 6, b, sel ? C_BG : C_DIM, 1, TC_DATUM);
  }
  snprintf(b, sizeof b, "%s  [%s]", s.name, s.gnss); txt(4, TOP + 22, b, C_FG);
  // sky plot
  int cx = 78, cy = 148, R = 62;
  for (int r = R; r > 0; r -= R / 3) spr.drawCircle(cx, cy, r, C_GRID * 2);
  spr.drawFastHLine(cx - R, cy, 2 * R, C_GRID * 2); spr.drawFastVLine(cx, cy - R, 2 * R, C_GRID * 2);
  txt(cx, cy - R - 10, "N", C_DIM, 1, TC_DATUM);
  for (int i = 0; i < s.count; i++) {
    Sat& t = s.sats[i]; float rr = R * (90 - t.el) / 90.f, a = t.az / RAD2DEG;
    int x = cx + (int)(rr * sinf(a)), y = cy - (int)(rr * cosf(a));
    uint16_t sc = snrCol(t.snr);
    spr.fillCircle(x, y, 6, sc); spr.drawCircle(x, y, 6, C_FG);
    snprintf(b, sizeof b, "%d", t.id); txt(x, y - 3, b, sc == TFT_BLACK || sc == TFT_RED ? C_FG : C_BG, 1, TC_DATUM);
  }
  // SNR bars
  int n = min(s.count, 11), bx = 160, bw = 150, rowH = 12;
  txt(bx, TOP + 34, "PRN  SNR", C_DIM);
  for (int i = 0; i < n; i++) {
    int y = TOP + 46 + i * rowH; snprintf(b, sizeof b, "%2d", s.sats[i].id); txt(bx, y, b, C_FG);
    bar(bx + 24, y, bw - 50, 9, s.sats[i].snr / 50.f, s.sats[i].snr > 0 ? snrCol(s.sats[i].snr) : C_DIM);
    snprintf(b, sizeof b, "%d", s.sats[i].snr); txt(bx + bw, y, b, C_FG, 1, TR_DATUM);
  }
  if (s.count == 0) txt(bx, TOP + 50, "no sats in view", C_BAD);
  // fix info
  char hd[12], us[12]; nz(S.hdop, "%.1f", 1, hd, sizeof hd); nz(S.satsUsed, "%.0f", 1, us, sizeof us);
  snprintf(b, sizeof b, "fix:%s used:%s hdop:%s", S.fix ? "YES" : "NO", us, hd);
  txt(4, H - 22, b, S.fix ? C_OK : C_BAD);
  snprintf(b, sizeof b, "age %us", (unsigned)((millis() - s.lastMs) / 1000)); txt(W - 4, H - 22, b, C_DIM, 1, TR_DATUM);
}

// ================= V_ATT =================
static void drawAttitude() {
  char b[32];
  if (page[V_ATT] == 0) {
    float roll = S.roll.ok() ? S.roll.v * RAD2DEG + ROLL_OFFSET_DEG : NAN;
    float pitch = S.pitch.ok() ? S.pitch.v * RAD2DEG + PITCH_OFFSET_DEG : NAN;
    // stern view: waterline tilts with roll, bow-up pitch shown as bar on right
    int cx = 110, cy = 130;
    if (!isnan(roll)) {
      float a = -roll / RAD2DEG;   // +ve heel to starboard -> boat leans right
      float s = sinf(a), c = cosf(a);
      auto P = [&](float x, float y, int& ox, int& oy) { ox = cx + (int)(x * c - y * s); oy = cy + (int)(x * s + y * c); };
      spr.drawFastHLine(cx - 100, cy + 14, 200, C_ACC);          // true horizon/waterline
      int x1, y1, x2, y2, x3, y3, x4, y4;                      // hull
      P(-50, 0, x1, y1); P(50, 0, x2, y2); P(30, 30, x3, y3); P(-30, 30, x4, y4);
      spr.fillTriangle(x1, y1, x2, y2, x3, y3, C_FG); spr.fillTriangle(x1, y1, x3, y3, x4, y4, C_FG);
      int mx, my, tx, ty; P(0, 0, mx, my); P(0, -80, tx, ty); spr.drawLine(mx, my, tx, ty, C_WARN);
    }
    nz(S.roll, "%.1f", RAD2DEG, b, sizeof b, ROLL_OFFSET_DEG);
    txt(110, TOP + 4, "HEEL", C_DIM, 1, TC_DATUM);
    uint16_t hc = (!isnan(roll) && fabsf(roll) > 20) ? C_BAD : (!isnan(roll) && fabsf(roll) > 10 ? C_WARN : C_OK);
    snprintf(b, sizeof b, "%s", isnan(roll) ? "--" : (roll >= 0 ? "stbd" : "port")); txt(110, TOP + 18, b, C_DIM, 1, TC_DATUM);
    if (!isnan(roll)) snprintf(b, sizeof b, "%.1f", fabsf(roll)); else strcpy(b, "--");
    txt(110, H - 40, b, hc, 4, TC_DATUM);
    // pitch bar
    txt(260, TOP + 4, "PITCH", C_DIM, 1, TC_DATUM);
    int bx = 245, by = TOP + 22, bh = 150;
    spr.drawRect(bx, by, 30, bh, C_DIM); spr.drawFastHLine(bx, by + bh / 2, 30, C_DIM);
    if (!isnan(pitch)) {
      int off = (int)constrain(pitch / 20.f * (bh / 2), -(bh / 2 - 1), bh / 2 - 1);
      spr.fillRect(bx + 2, off < 0 ? by + bh / 2 + off : by + bh / 2 - off + (off > 0 ? 0 : 0), 26, 3, C_ACC);
      spr.fillRect(bx + 2, by + bh / 2 - off - 1, 26, 3, C_ACC);
      snprintf(b, sizeof b, "%.1f", pitch);
    } else strcpy(b, "--");
    txt(260, H - 40, b, C_ACC, 3, TC_DATUM);
    txt(260, H - 14, "bow up +", C_DIM, 1, TC_DATUM);
  } else {
    // weather / environment
    char v[16]; int y = TOP + 8, row = 24;
    auto line = [&](const char* label, const Num& n, const char* fmt, float scale, float off, const char* unit) {
      nz(n, fmt, scale, v, sizeof v, off);
      txt(8, y, label, C_DIM, 2); txt(190, y, v, n.ok() ? C_FG : C_DIM, 2, TR_DATUM); txt(196, y + 4, unit, C_DIM); y += row; };
    txt(8, y - 4, S.fix ? "current location" : "no position - using server data", C_DIM); y += 12;
    line("Air temp", S.airTemp, "%.1f", 1, -273.15f, "C");
    line("Water", S.waterTemp, "%.1f", 1, -273.15f, "C");
    line("Pressure", S.pressure, "%.0f", 0.01f, 0, "hPa");
    line("Humidity", S.humidity, "%.0f", 100, 0, "%");
    line("App wind", S.awsp, "%.1f", MS2KN, 0, "kn");
    line("True wind", S.twsp, "%.1f", MS2KN, 0, "kn");
    line("Depth", S.depth, "%.1f", 1, 0, "m");
    // wind angle
    int cx = 262, cy = TOP + 100, R = 42; spr.drawCircle(cx, cy, R, C_DIM);
    txt(cx, cy - R - 10, "bow", C_DIM, 1, TC_DATUM);
    if (S.awa.ok()) { float a = S.awa.v; spr.drawLine(cx, cy, cx + (int)(R * sinf(a)), cy - (int)(R * cosf(a)), C_ACC); }
    if (!S.airTemp.ok() && !S.awsp.ok() && !S.pressure.ok())
      txt(W / 2, H - 14, "no environment.* data on server", C_WARN, 1, TC_DATUM);
  }
}

// ================= V_DEBUG =================
static void drawDebug() {
  char b[64]; int y = TOP + 6, row = 13;
  auto kv = [&](const char* k, const char* v, uint16_t col = C_FG) { txt(6, y, k, C_DIM); txt(110, y, v, col); y += row; };
  if (page[V_DEBUG] == 0) {
    snprintf(b, sizeof b, "%s (st %d)", S.wifi ? WiFi.SSID().c_str() : "disconnected", S.wifiStatus); kv("WiFi", b, S.wifi ? C_OK : C_BAD);
    snprintf(b, sizeof b, "%s  %ddBm", S.wifi ? WiFi.localIP().toString().c_str() : "-", S.wifi ? WiFi.RSSI() : 0); kv("IP / RSSI", b);
    snprintf(b, sizeof b, "%s:%d", SK_HOST, SK_PORT); kv("Signal K", b);
    kv("WebSocket", S.ws ? "connected" : "down", S.ws ? C_OK : C_BAD);
    kv("Access", S.auth, S.authed ? C_OK : C_WARN);
    snprintf(b, sizeof b, "%u/s  total %u", (unsigned)S.msgRate, (unsigned)S.msgCount); kv("Deltas", b);
    snprintf(b, sizeof b, "%us ago", S.lastMsgMs ? (unsigned)((millis() - S.lastMsgMs) / 1000) : 0); kv("Last msg", b);
    snprintf(b, sizeof b, "%u  (dropped %u)", (unsigned)S.reconnects, (unsigned)S.dropped); kv("Connects", b);
    kv("Last error", S.lastErr[0] ? S.lastErr : "none", S.lastErr[0] ? C_WARN : C_DIM);
    snprintf(b, sizeof b, "%um %us", (unsigned)(millis() / 60000), (unsigned)((millis() / 1000) % 60)); kv("Uptime", b);
    snprintf(b, sizeof b, "fix src: %s", S.fixSrc[0] ? S.fixSrc : "-"); kv("GPS", b);
    snprintf(b, sizeof b, "%s", relayEnabled() ? relayStatus() : "not configured"); kv("Tile relay", b, relayEnabled() ? C_FG : C_DIM);
  } else {
    txt(6, y, "Raspberry Pi (signalk-rpi-stats)", C_ACC); y += row + 4;
    char v[16];
    nz(S.piCpuTemp, "%.1f C", 1, v, sizeof v, -273.15f); kv("CPU temp", v, S.piCpuTemp.ok() && S.piCpuTemp.v > 343 ? C_BAD : C_FG);
    nz(S.piCpuUtil, "%.0f %%", 100, v, sizeof v); kv("CPU use", v);
    nz(S.piLoad1, "%.2f", 1, v, sizeof v); kv("Load 1m", v);
    nz(S.piMem, "%.0f %%", 100, v, sizeof v); kv("Memory", v);
    nz(S.piDisk, "%.0f %%", 100, v, sizeof v); kv("Storage", v);
    nz(S.piVolt, "%.3f V", 1, v, sizeof v); kv("Core volt", v);
    if (S.piUptime.ok()) { uint32_t u = S.piUptime.v; snprintf(v, sizeof v, "%ud %uh %um", u / 86400, (u / 3600) % 24, (u / 60) % 60); } else strcpy(v, "--");
    kv("Pi uptime", v);
    kv("Throttled", S.piThrottled ? "YES" : "no", S.piThrottled ? C_BAD : C_OK);
    nz(S.noise, "%.4f", 1, v, sizeof v); kv("Noise (tx)", v);
    kv("Display", dnStatus());
    kv("Under-volt", S.piUnderV ? "YES" : "no", S.piUnderV ? C_BAD : C_OK);
  }
}

// ================= public =================
void viewsBegin() {
  tft.begin(); tft.setRotation(3); tft.fillScreen(C_BG);
  spr.setColorDepth(8); spr.createSprite(W, H);
}

// Night mode: dim red-only picture (keeps night vision). The backlight pin has no PWM, so it can't be dimmed. The alarm overlay is drawn afterwards in full colour.
static void nightFilter() {
  static uint8_t lut[256]; static bool init = false;
  if (!init) {
    for (int i = 0; i < 256; i++) {   // RGB332 -> luminance -> dim red
      int r = ((i >> 5) & 7) * 255 / 7, g = ((i >> 2) & 7) * 255 / 7, b = (i & 3) * 255 / 3;
      int lum = (r * 30 + g * 59 + b * 11) / 100;
      lut[i] = (uint8_t)((lum > 16 ? 1 + lum * 2 / 255 : 0) << 5);   // dim red, levels 0-3 of 7
    }
    init = true;
  }
  uint8_t* p = (uint8_t*)spr.getPointer();
  for (int i = 0; i < W * H; i++) p[i] = lut[p[i]];
}
// ---- alarm overlay (Signal K notifications): shown over every view until dismissed ----
static void drawAlarmOverlay() {
  const Alarm* a = alarmTop();
  int active = alarmActiveCount();
  if (!a) {                                   // nothing to show; badge for dismissed-but-still-active alarms
    if (active) { spr.fillRoundRect(W - 62, 2, 28, 16, 3, TFT_ORANGE); txt(W - 48, 6, "!", C_BG, 1, TC_DATUM); }
    return;
  }
  bool flash = (millis() / 500) & 1;
  uint16_t bg = a->level >= AL_ALARM ? (flash ? TFT_RED : 0x8000) : a->level == AL_WARN ? TFT_ORANGE : 0x8400;
  const int x = 12, y = 36, w = W - 24, h = 150;
  spr.fillRect(x, y, w, h, bg); spr.drawRect(x, y, w, h, C_FG); spr.drawRect(x + 1, y + 1, w - 2, h - 2, C_FG);
  txt(W / 2, y + 8, alarmLevelName(a->level), C_FG, 3, TC_DATUM);
  // name from the path, e.g. "navigation.anchor"
  char nm[40]; strlcpy(nm, a->path, sizeof nm); txt(W / 2, y + 40, nm, TFT_YELLOW, 1, TC_DATUM);
  // message, wrapped at word boundaries, size 2 (about 22 chars per line)
  const char* m = a->msg[0] ? a->msg : "(no message)";
  char line[24]; int ly = y + 56;
  for (int row = 0; row < 4 && *m; row++) {
    int n = (int)strlen(m); if (n > 22) { n = 22; int k = n; while (k > 8 && m[k] != ' ') k--; if (m[k] == ' ') n = k; }
    memcpy(line, m, n); line[n] = 0; txt(W / 2, ly, line, C_FG, 2, TC_DATUM); ly += 18;
    m += n; while (*m == ' ') m++;
  }
  int more = alarmUnackedCount() - 1;
  char f[40]; if (more > 0) snprintf(f, sizeof f, "[B] dismiss   +%d more", more); else snprintf(f, sizeof f, "[B] or joystick press: dismiss");
  txt(W / 2, y + h - 12, f, C_FG, 1, TC_DATUM);
}

void viewsDraw(int view) {
  spr.fillSprite(C_BG);
  switch (view) {
    case V_CHART: drawChart(); break;
    case V_SATS: drawSats(); break;
    case V_ATT: drawAttitude(); break;
    case V_DEBUG: drawDebug(); break;
  }
  header(view);
  // button hints
  static const char* hintB[V_COUNT] = {"B:zoom", "[B] next GPS source", "[B] next page", "[B] next page"};
  spr.setTextDatum(BC_DATUM); spr.setTextColor(C_DIM); spr.setTextSize(1);
  if (view != V_CHART) spr.drawString(hintB[view], W / 2, H - 1);
  bool night = dnIsNight();
  if (night) nightFilter();
  drawAlarmOverlay();
  spr.pushSprite(0, 0);
}
void viewsButtonB(int view) {
  switch (view) {
    case V_CHART: zoom = (zoom + 1) % (sizeof ZOOM_NM / sizeof *ZOOM_NM); break;
    case V_SATS: if (S.nSrc) srcSel = (srcSel + 1) % S.nSrc; break;
    case V_ATT: case V_DEBUG: page[view] ^= 1; break;
  }
}
void viewsJoystick(int view, int dx, int dy, bool press) {
  if (view != V_CHART) return;
  float stepM = ZOOM_NM[zoom] * 1852.f * 0.4f;
  if (press) { follow = true; panN = panE = 0; if (S.fix) { cLat = S.lat; cLon = S.lon; } return; }
  if (dx || dy) {
    if (follow) { follow = false; if (S.fix) { cLat = S.lat; cLon = S.lon; } }
    panE += dx * stepM; panN += -dy * stepM;
  }
}
