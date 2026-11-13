#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Signal K notifications -> on-screen alarm overlay + buzzer.
// Levels follow the Signal K notification states.
enum { AL_NORMAL = 0, AL_ALERT = 1, AL_WARN = 2, AL_ALARM = 3, AL_EMERGENCY = 4 };

struct Alarm {
  bool used = false, ack = false, beeped = false;
  uint8_t level = 0;
  char path[48] = "";     // without the "notifications." prefix
  char msg[72] = "";
  uint32_t sinceMs = 0;
};

void alarmUpdate(const char* path, JsonVariantConst v);   // path = full Signal K path, v = notification value (or null = cleared)
const Alarm* alarmTop();         // highest-level un-dismissed alarm, or nullptr
int alarmUnackedCount();         // un-dismissed alarms (including the top one)
int alarmActiveCount();          // all active alarms, dismissed or not
void alarmDismiss();             // dismiss the top alarm (silences it; it returns if it escalates or re-triggers)
void alarmTick();                // drives the buzzer; call every loop
const char* alarmLevelName(int level);
