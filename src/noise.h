#pragma once
// Samples the on-board microphone and publishes environment.noise.level to Signal K.
// Level is a unitless 0..1 RMS of the microphone signal (1.0 = full ADC swing), not calibrated decibels.
void noiseLoop();
