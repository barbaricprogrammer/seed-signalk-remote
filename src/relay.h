#pragma once
#include <stdint.h>
bool relayEnabled();
// Ask the tile relay for a chart image if the view moved/zoomed away from the cached one (rate limited, may block ~1 s).
void relayUpdate(double lat, double lon, float mpp);
// Copy the cached image into an 8-bit (RGB332) sprite buffer, shifted to the current centre. Returns false if none yet.
bool relayBlit(uint8_t* buf, int W, int H, int centreY, double lat, double lon, float mpp);
const char* relayStatus();
