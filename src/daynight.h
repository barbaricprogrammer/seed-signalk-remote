#pragma once
#include <Arduino.h>
// Day/night from Signal K: GPS time (navigation.datetime) + dawn/dusk from environment.sunlight.times.*
void dnTime(const char* suffix, const char* iso);   // suffix after "environment.sunlight.times." (e.g. "dusk" or "1.dawn")
void dnDatetime(const char* iso);                   // navigation.datetime
bool dnIsNight();                                   // false when unknown
const char* dnStatus();                             // short text for the debug page
