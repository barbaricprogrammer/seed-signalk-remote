#pragma once
#include <Arduino.h>

struct Sat { uint8_t id; int16_t el, az; int16_t snr; };
struct SatSource {
  char name[28] = "";      // "$source" e.g. grove-gps.GP
  char gnss[10] = "";      // GPS / GLONASS / ...
  int count = 0;
  Sat sats[24];
  uint32_t lastMs = 0;
};

struct Num {               // value that may be missing / stale
  float v = NAN; uint32_t ms = 0;
  bool ok(uint32_t maxAge = 15000) const { return !isnan(v) && millis() - ms < maxAge; }
  void set(float x) { v = x; ms = millis(); }
};

struct State {
  // navigation
  bool fix = false; double lat = 0, lon = 0; uint32_t posMs = 0;
  Num sog, cog, hdg, hdgMag, magVar, roll, pitch, hdop, satsUsed;
  char fixSrc[28] = "";
  // gps sources
  SatSource src[6]; int nSrc = 0;
  // environment
  Num noise, airTemp, waterTemp, pressure, humidity, depth;
  Num awsp, awa, twsp, twa;
  // pi
  Num piCpuTemp, piCpuUtil, piMem, piDisk, piLoad1, piUptime, piVolt;
  bool piThrottled = false, piUnderV = false;
  // link / debug
  int wifiStatus = 0;
  bool wifi = false, ws = false;
  uint32_t msgCount = 0, msgRate = 0, lastMsgMs = 0, reconnects = 0, dropped = 0;
  char lastErr[40] = "";
  char auth[24] = "no token";   // access-request state shown on debug view
  bool authed = false;
  char clientId[28] = "";
};
extern State S;
