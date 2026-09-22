// Defender node (headband or wrist). Same runtime as the attacker, own model.
// Copy ml/out/defender/model.h here once trained.
//
// arduino-cli compile -b arduino:mbed_nano:nano33ble --library ../libraries/SlapCommon .
#if __has_include("model.h")
#include "model.h"
#define SLAP_HAVE_MODEL 1
#endif

#include <slap_node.h>

#ifdef SLAP_HAVE_MODEL
#include <slap_classifier_tflm.h>
#define CLASSIFY slapTflmClassify
#else
#include <slap_classifier_heuristic.h>
#define CLASSIFY slapHeuristicClassify<ROLE_DEFENDER>
#endif

// Dodges are softer than slaps: lower thresholds.
#ifndef SLAP_ACC_TRIGGER_G
#define SLAP_ACC_TRIGGER_G 1.6f
#endif
#ifndef SLAP_GYRO_TRIGGER_DPS
#define SLAP_GYRO_TRIGGER_DPS 250.0f
#endif

SlapNode node({ROLE_DEFENDER, SLAP_NAME_DEFENDER, SLAP_ACC_TRIGGER_G, SLAP_GYRO_TRIGGER_DPS,
               /*conf_min=*/170, /*cooldown_ms=*/400},
              CLASSIFY);

void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && millis() - t < 2000) {}
  if (!node.begin()) while (true) {}
#ifdef SLAP_HAVE_MODEL
  node.reportArena(slapTflmBegin());
#else
  Serial.println("INFO,classifier=heuristic");
#endif
}

void loop() {
  node.loop();
  node.service();
}
