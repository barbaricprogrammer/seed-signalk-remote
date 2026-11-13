#include "daynight.h"

static uint32_t dawn[2], dusk[2];          // epoch seconds, [0] = today's set, [1] = tomorrow's set (0 = unknown)
static uint32_t gpsEpoch = 0, gpsMs = 0;   // GPS time and the millis() when it arrived

static uint32_t parseIso(const char* s) {  // "2026-11-13T22:21:46.132Z" -> unix seconds, 0 on error
  int Y, M, D, h, m, sec;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6 || Y < 2020) return 0;
  Y -= M <= 2; int era = Y / 400, yoe = Y - era * 400;
  int doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097 + doe - 719468;
  return (uint32_t)(days * 86400L + h * 3600L + m * 60L + sec);
}

void dnDatetime(const char* iso) { uint32_t e = parseIso(iso); if (e) { gpsEpoch = e; gpsMs = millis(); } }

void dnTime(const char* suffix, const char* iso) {
  int slot = 0;
  if (suffix[0] == '1' && suffix[1] == '.') { slot = 1; suffix += 2; }
  uint32_t e = parseIso(iso); if (!e) return;
  if (!strcmp(suffix, "dawn")) dawn[slot] = e;
  else if (!strcmp(suffix, "dusk")) dusk[slot] = e;
}

static uint32_t nowEpoch() { return gpsEpoch ? gpsEpoch + (millis() - gpsMs) / 1000 : 0; }

bool dnIsNight() {
  uint32_t now = nowEpoch(); if (!now) return false;
  uint32_t nd = 0xFFFFFFFF, nu = 0xFFFFFFFF, md = 0, mu = 0;
  for (int i = 0; i < 2; i++) {
    if (dawn[i]) { if (dawn[i] > now && dawn[i] < nd) nd = dawn[i]; if (dawn[i] > md) md = dawn[i]; }
    if (dusk[i]) { if (dusk[i] > now && dusk[i] < nu) nu = dusk[i]; if (dusk[i] > mu) mu = dusk[i]; }
  }
  if (nd != 0xFFFFFFFF || nu != 0xFFFFFFFF) return nd < nu;   // next event is dawn -> it is night now
  return mu > md;                                              // all times stale: last known event
}

const char* dnStatus() {
  static char b[40];
  if (!nowEpoch()) return "no GPS time";
  if (!dawn[0] && !dawn[1] && !dusk[0] && !dusk[1]) return "no dawn/dusk data";
  snprintf(b, sizeof b, "%s (auto)", dnIsNight() ? "night" : "day");
  return b;
}
