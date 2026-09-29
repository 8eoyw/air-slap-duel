// Player node: one per player, strapped to the back of the slapping hand, kept
// on for the whole match. Runs the attacker and the defender pipeline on every
// motion and sends whatever either one recognises; the referee keeps the class
// that fits the moment (slap / feint, dodge / block, counter, feint follow-up).
//
// Flash one board with -DSLAP_PLAYER=1 (SLAP-P1) and the other with -DSLAP_PLAYER=2 (SLAP-P2):
//   arduino-cli compile -b arduino:mbed_nano:nano33ble --library ../libraries/SlapCommon \
//     --build-property "compiler.cpp.extra_flags=-DSLAP_PLAYER=2" .
//
// Models: copy ml/out/attacker/model.h -> attacker_model.h and
//              ml/out/defender/model.h -> defender_model.h (next to this file).
// Without both -> heuristic classifiers, enough to test BLE + referee end to end.
#ifndef SLAP_PLAYER
#define SLAP_PLAYER 1
#endif

#if __has_include("attacker_model.h") && __has_include("defender_model.h")
#define SLAP_MODEL_NO_LEGACY
#include "attacker_model.h"
#include "defender_model.h"
#define SLAP_HAVE_MODELS 1
static_assert(slap_model_attacker::kHz == slap_model_defender::kHz,
              "both models must be trained at the same sample rate");
#define SAMPLE_HZ (slap_model_attacker::kHz)
#define SLAP_RING (slap_model_attacker::kWindow > slap_model_defender::kWindow \
                       ? slap_model_attacker::kWindow : slap_model_defender::kWindow)
#endif

#include <slap_node.h>

#ifdef SLAP_HAVE_MODELS
#include <slap_classifier_tflm.h>
// Two arenas of 40 KB; the boot log prints what each one really uses.
SLAP_TFLM_INSTANCE(atkModel, slap_model_attacker, 40 * 1024);
SLAP_TFLM_INSTANCE(defModel, slap_model_defender, 40 * 1024);

static const SlapPipelineConfig kPipes[] = {
    {"atk", [](const float* w, uint8_t* c, uint8_t* f) { atkModel.classify(w, c, f); },
     slap_model_attacker::kWindow, slap_model_attacker::kPre, slap_model_attacker::kAccTriggerG,
     slap_model_attacker::kGyroTriggerDps, /*conf_min=*/180, slap_model_attacker::kCooldownMs},
    {"def", [](const float* w, uint8_t* c, uint8_t* f) { defModel.classify(w, c, f); },
     slap_model_defender::kWindow, slap_model_defender::kPre, slap_model_defender::kAccTriggerG,
     slap_model_defender::kGyroTriggerDps, /*conf_min=*/170, slap_model_defender::kCooldownMs},
};
#else
#include <slap_classifier_heuristic.h>
// Same thresholds as nano_attacker / nano_defender (and ml/slapml.py TRIGGERS).
static const SlapPipelineConfig kPipes[] = {
    {"atk", slapHeuristicClassify<ROLE_ATTACKER>, SLAP_WINDOW, SLAP_PRE, 2.5f, 600.0f, 180, 400},
    {"def", slapHeuristicClassify<ROLE_DEFENDER>, SLAP_WINDOW, SLAP_PRE, 1.6f, 250.0f, 170, 400},
};
#endif

SlapNode node(ROLE_PLAYER, SLAP_PLAYER == 2 ? SLAP_NAME_P2 : SLAP_NAME_P1, kPipes, 2);

void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && millis() - t < 2000) {}  // don't block when running on battery
  if (!node.begin()) while (true) {}
#ifdef SLAP_HAVE_MODELS
  node.reportArena(atkModel.begin());
  node.reportArena(defModel.begin());
#else
  Serial.println("INFO,classifier=heuristic");
#endif
}

void loop() {
  node.loop();
  node.service();
}
