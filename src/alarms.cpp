#include "alarms.h"

static const int MAXA = 8;
static Alarm al[MAXA];
static const int BUZZ_HZ = 2700;

static int levelOf(const char* st) {
  if (!st) return AL_NORMAL;
  if (!strcmp(st, "alert")) return AL_ALERT;
  if (!strcmp(st, "warn")) return AL_WARN;
  if (!strcmp(st, "alarm")) return AL_ALARM;
  if (!strcmp(st, "emergency")) return AL_EMERGENCY;
  return AL_NORMAL;                       // "normal", "nominal", anything unknown
}

const char* alarmLevelName(int l) {
  switch (l) { case AL_ALERT: return "ALERT"; case AL_WARN: return "WARNING"; case AL_ALARM: return "ALARM"; case AL_EMERGENCY: return "EMERGENCY"; }
  return "";
}

static Alarm* find(const char* p) {
  for (int i = 0; i < MAXA; i++) if (al[i].used && !strcmp(al[i].path, p)) return &al[i];
  return nullptr;
}

void alarmUpdate(const char* fullPath, JsonVariantConst v) {
  const char* p = fullPath + (strncmp(fullPath, "notifications.", 14) ? 0 : 14);
  int lvl = v.isNull() ? AL_NORMAL : levelOf(v["state"] | "normal");
  Alarm* a = find(p);
  if (lvl == AL_NORMAL) { if (a) a->used = false; return; }         // cleared
  if (!a) {
    for (int i = 0; i < MAXA; i++) if (!al[i].used) { a = &al[i]; break; }
    if (!a) return;                                                  // table full: drop (should be rare)
    *a = Alarm(); a->used = true; a->sinceMs = millis(); strlcpy(a->path, p, sizeof a->path);
  } else if (lvl > a->level) { a->ack = false; a->beeped = false; a->sinceMs = millis(); }   // escalation
  a->level = lvl;
  strlcpy(a->msg, v["message"] | "", sizeof a->msg);
}

const Alarm* alarmTop() {
  const Alarm* t = nullptr;
  for (int i = 0; i < MAXA; i++) if (al[i].used && !al[i].ack && (!t || al[i].level > t->level)) t = &al[i];
  return t;
}
int alarmUnackedCount() { int n = 0; for (int i = 0; i < MAXA; i++) n += al[i].used && !al[i].ack; return n; }
int alarmActiveCount()  { int n = 0; for (int i = 0; i < MAXA; i++) n += al[i].used; return n; }
void alarmDismiss() { const Alarm* t = alarmTop(); if (t) const_cast<Alarm*>(t)->ack = true; }

static bool buzzing = false;
static void buzz(bool on) {
  if (on == buzzing) return;
  buzzing = on;
  if (on) tone(WIO_BUZZER, BUZZ_HZ); else noTone(WIO_BUZZER);
}

void alarmTick() {
  const Alarm* t = alarmTop();
  if (!t) { buzz(false); return; }
  uint32_t now = millis(), age = now - t->sinceMs;
  bool on = false;
  switch (t->level) {
    case AL_ALERT:      on = !t->beeped ? (age < 300) : false; if (age >= 300) const_cast<Alarm*>(t)->beeped = true; break;   // one beep
    case AL_WARN:       { uint32_t m = now % 10000; on = m < 150 || (m >= 300 && m < 450); } break;                        // double beep every 10 s
    case AL_ALARM:      on = (now % 600) < 250; break;                                                                      // steady beeping
    case AL_EMERGENCY:  on = (now % 240) < 120; break;                                                                      // fast beeping
  }
  buzz(on);
}
