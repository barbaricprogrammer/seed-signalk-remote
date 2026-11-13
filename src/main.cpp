#include <Arduino.h>
#include "state.h"
#include "signalk.h"
#include "views.h"
#include "alarms.h"
#include "noise.h"

static int view = V_CHART;
static uint32_t lastDraw = 0, lastRepeat = 0;
static bool prevA, prevB, prevC;
static const int PIN_5S[5] = {WIO_5S_LEFT, WIO_5S_RIGHT, WIO_5S_UP, WIO_5S_DOWN, WIO_5S_PRESS};

void setup() {
  Serial.begin(115200);
  pinMode(WIO_KEY_A, INPUT_PULLUP); pinMode(WIO_KEY_B, INPUT_PULLUP); pinMode(WIO_KEY_C, INPUT_PULLUP);
  for (int p : PIN_5S) pinMode(p, INPUT_PULLUP);
  randomSeed(analogRead(0) ^ micros());
  viewsBegin();
  skBegin();
}

void loop() {
  skLoop();
  noiseLoop();
  alarmTick();
  uint32_t now = millis();
  bool a = !digitalRead(WIO_KEY_A), b = !digitalRead(WIO_KEY_B), c = !digitalRead(WIO_KEY_C);
  bool redraw = false;
  // Wio keys: A is the left-most on the top edge, C the right-most (as seen with the screen facing you)
  if (a && !prevA) { view = (view + V_COUNT - 1) % V_COUNT; redraw = true; }
  if (c && !prevC) { view = (view + 1) % V_COUNT; redraw = true; }
  if (b && !prevB) { if (alarmTop()) alarmDismiss(); else viewsButtonB(view); redraw = true; }
  prevA = a; prevB = b; prevC = c;

  int dx = 0, dy = 0; bool press = !digitalRead(WIO_5S_PRESS);
  if (!digitalRead(WIO_5S_LEFT)) dx = -1; if (!digitalRead(WIO_5S_RIGHT)) dx = 1;
  if (!digitalRead(WIO_5S_UP)) dy = -1;   if (!digitalRead(WIO_5S_DOWN)) dy = 1;
  if ((dx || dy || press) && now - lastRepeat > 180) {
    lastRepeat = now; redraw = true;
    if (alarmTop()) { if (press) alarmDismiss(); }          // overlay showing: joystick press dismisses, movement is ignored
    else viewsJoystick(view, dx, dy, press);
  }

  if (redraw || now - lastDraw > 500) { lastDraw = now; viewsDraw(view); }
}
