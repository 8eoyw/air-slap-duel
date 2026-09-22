// Stand-in classifier for before a model is trained: lets BLE, sync and the
// referee be integrated in weeks 1-2. Rules are deliberately crude.
//   attacker: sign of the dominant gyro axis -> forehand / backhand
//   defender: sign of lateral accel (ay)     -> dodge_left / dodge_right
// Axis choices depend on how the board is strapped; flip the signs if needed.
#pragma once
#include "slap_protocol.h"

#ifndef SLAP_WINDOW
#define SLAP_WINDOW 100
#endif

template <SlapRole R>
void slapHeuristicClassify(const float* win, uint8_t* cls, uint8_t* conf) {
  float sum[6] = {0};
  for (int i = 0; i < SLAP_WINDOW; i++)
    for (int c = 0; c < 6; c++) sum[c] += win[i * 6 + c];

  if (R == ROLE_ATTACKER) {
    // Pick the gyro axis with the largest integrated rotation.
    int best = 3;
    for (int c = 4; c < 6; c++)
      if (fabsf(sum[c]) > fabsf(sum[best])) best = c;
    float mag = fabsf(sum[best]) / SLAP_WINDOW;  // mean dps over the window
    *cls = sum[best] > 0 ? CLS_FOREHAND : CLS_BACKHAND;
    *conf = mag > 150 ? 255 : (uint8_t)(mag * 255 / 150);
  } else {
    // Remove gravity roughly by subtracting the window mean of the first 10 samples.
    float base = 0;
    for (int i = 0; i < 10; i++) base += win[i * 6 + 1];
    base /= 10;
    float lat = sum[1] / SLAP_WINDOW - base;
    *cls = lat > 0 ? CLS_DODGE_RIGHT : CLS_DODGE_LEFT;
    float mag = fabsf(lat);
    *conf = mag > 0.3f ? 255 : (uint8_t)(mag * 255 / 0.3f);
  }
}
