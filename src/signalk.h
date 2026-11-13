#pragma once
void skBegin();
void skLoop();
bool skPublish(const char* path, float value);   // send a delta to Signal K (false if not connected)
