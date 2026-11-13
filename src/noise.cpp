#include <Arduino.h>
#include <math.h>
#include "noise.h"
#include "state.h"
#include "signalk.h"

void noiseLoop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if ((int32_t)(now - last) < 2000) return;
  last = now;
  analogReadResolution(12);
  const int N = 200;
  static uint16_t buf[200];
  for (int i = 0; i < N; i++) buf[i] = analogRead(WIO_MIC);
  float mean = 0; for (int i = 0; i < N; i++) mean += buf[i]; mean /= N;
  float acc = 0;  for (int i = 0; i < N; i++) { float d = buf[i] - mean; acc += d * d; }
  float level = sqrtf(acc / N) / 2048.f;
  S.noise.set(level);
  skPublish("environment.noise.level", level);
}
