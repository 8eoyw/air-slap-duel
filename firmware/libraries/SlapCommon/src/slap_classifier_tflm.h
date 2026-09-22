// TFLite Micro classifier (library: Chirale_TensorFLowLite 2.x).
// Include AFTER the exported "model.h", which defines:
//   g_model[], g_model_len, SLAP_N_CLASSES, SLAP_CLASS_IDS[], SLAP_MEAN[6], SLAP_STD[6]
// and SLAP_WINDOW / SLAP_PRE / trigger thresholds used during training.
#pragma once
#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#ifndef SLAP_ARENA_BYTES
#define SLAP_ARENA_BYTES (40 * 1024)
#endif

namespace slap_tflm {
alignas(16) static uint8_t arena[SLAP_ARENA_BYTES];
static tflite::MicroInterpreter* interp = nullptr;
static TfLiteTensor* in = nullptr;
static TfLiteTensor* out = nullptr;
}  // namespace slap_tflm

// Returns arena bytes used, or 0 on failure.
inline size_t slapTflmBegin() {
  using namespace slap_tflm;
  const tflite::Model* model = tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("INFO,error=tflite_schema");
    return 0;
  }
  static tflite::AllOpsResolver resolver;
  static tflite::MicroInterpreter mi(model, resolver, arena, SLAP_ARENA_BYTES);
  interp = &mi;
  if (interp->AllocateTensors() != kTfLiteOk) {
    Serial.println("INFO,error=allocate_tensors");
    return 0;
  }
  in = interp->input(0);
  out = interp->output(0);
  return interp->arena_used_bytes();
}

inline void slapTflmClassify(const float* win, uint8_t* cls, uint8_t* conf) {
  using namespace slap_tflm;
  *cls = CLS_BACKGROUND;
  *conf = 0;
  if (!interp) return;

  const bool q = in->type == kTfLiteInt8;
  const float is = q ? in->params.scale : 1.0f;
  const int iz = q ? in->params.zero_point : 0;
  for (int i = 0; i < SLAP_WINDOW; i++) {
    for (int c = 0; c < 6; c++) {
      float x = (win[i * 6 + c] - SLAP_MEAN[c]) / SLAP_STD[c];
      int k = i * 6 + c;
      if (q) {
        int v = (int)lroundf(x / is) + iz;
        in->data.int8[k] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
      } else {
        in->data.f[k] = x;
      }
    }
  }
  if (interp->Invoke() != kTfLiteOk) return;

  int best = 0;
  float bestP = -1;
  for (int k = 0; k < SLAP_N_CLASSES; k++) {
    float p = out->type == kTfLiteInt8
                  ? (out->data.int8[k] - out->params.zero_point) * out->params.scale
                  : out->data.f[k];
    if (p > bestP) { bestP = p; best = k; }
  }
  *cls = SLAP_CLASS_IDS[best];
  *conf = (uint8_t)(bestP <= 0 ? 0 : bestP >= 1 ? 255 : bestP * 255);
}
