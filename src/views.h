#pragma once
#include <TFT_eSPI.h>
enum { V_CHART, V_SATS, V_ATT, V_DEBUG, V_COUNT };
void viewsBegin();
void viewsDraw(int view);                 // render current view into sprite and push
void viewsButtonB(int view);              // page / zoom / source change
void viewsJoystick(int view, int dx, int dy, bool press);
