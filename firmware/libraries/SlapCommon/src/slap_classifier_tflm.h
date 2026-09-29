// TFLite Micro classifier (library: Chirale_TensorFLowLite 2.x).
// Include AFTER the exported model header(s) from ml/train.py. Each defines
// namespace slap_model_<task> { kModel, kWindow, kNClasses, kClassIds, kMean, kStd, ... }.
//
// One model (nano_attacker / nano_defender): model.h also provides the legacy
// SLAP_* / g_model aliases, and slapTflmBegin() / slapTflmClassify() below use them.
//
// Several models (nano_player): define SLAP_MODEL_NO_LEGACY, include each header,
// and make one SlapTflm per model with SLAP_TFLM_INSTANCE(name, namespace, arena_bytes).
#pragma once
#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#ifndef SLAP_ARENA_BYTES
#define SLAP_ARENA_BYTES (40 * 1024)
#endif

class SlapTflm {
 public:
  SlapTflm(const unsigned char* model, int window, int n_classes, const uint8_t* class_ids,
           const float* mean, const float* std, uint8_t* arena, size_t arena_bytes)
      : model_(model), window_(window), n_(n_classes), ids_(class_ids), mean_(mean), std_(std),
        arena_(arena), arena_bytes_(arena_bytes) {}

  // Returns arena bytes used, or 0 on failure.
  size_t begin() {
    const tflite::Model* model = tflite::GetModel(model_);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
      Serial.println("INFO,error=tflite_schema");
      return 0;
    }
    static tflite::AllOpsResolver resolver;  // shared by every instance
    interp_ = new tflite::MicroInterpreter(model, resolver, arena_, arena_bytes_);
    if (interp_->AllocateTensors() != kTfLiteOk) {
      Serial.println("INFO,error=allocate_tensors");
      interp_ = nullptr;
      return 0;
    }
    in_ = interp_->input(0);
    out_ = interp_->output(0);
    return interp_->arena_used_bytes();
  }

  int window() const { return window_; }

  // `win` is window() x 6 floats, oldest first (see SlapClassifyFn in slap_node.h).
  void classify(const float* win, uint8_t* cls, uint8_t* conf) {
    *cls = CLS_BACKGROUND;
    *conf = 0;
    if (!interp_) return;

    const bool q = in_->type == kTfLiteInt8;
    const float is = q ? in_->params.scale : 1.0f;
    const int iz = q ? in_->params.zero_point : 0;
    for (int i = 0; i < window_; i++) {
      for (int c = 0; c < 6; c++) {
        float x = (win[i * 6 + c] - mean_[c]) / std_[c];
        int k = i * 6 + c;
        if (q) {
          int v = (int)lroundf(x / is) + iz;
          in_->data.int8[k] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
        } else {
          in_->data.f[k] = x;
        }
      }
    }
    if (interp_->Invoke() != kTfLiteOk) return;

    int best = 0;
    float bestP = -1;
    for (int k = 0; k < n_; k++) {
      float p = out_->type == kTfLiteInt8
                    ? (out_->data.int8[k] - out_->params.zero_point) * out_->params.scale
                    : out_->data.f[k];
      if (p > bestP) { bestP = p; best = k; }
    }
    *cls = ids_[best];
    *conf = (uint8_t)(bestP <= 0 ? 0 : bestP >= 1 ? 255 : bestP * 255);
  }

 private:
  const unsigned char* model_;
  int window_, n_;
  const uint8_t* ids_;
  const float *mean_, *std_;
  uint8_t* arena_;
  size_t arena_bytes_;
  tflite::MicroInterpreter* interp_ = nullptr;
  TfLiteTensor* in_ = nullptr;
  TfLiteTensor* out_ = nullptr;
};

// Declares `SlapTflm name` for the model in namespace `ns`, with its own arena.
#define SLAP_TFLM_INSTANCE(name, ns, arena_bytes)                                        \
  alignas(16) static uint8_t name##_arena[arena_bytes];                                  \
  static SlapTflm name(ns::kModel, ns::kWindow, ns::kNClasses, ns::kClassIds, ns::kMean, \
                       ns::kStd, name##_arena, arena_bytes)

#ifdef SLAP_N_CLASSES  // single-model sketch: legacy aliases from model.h
alignas(16) static uint8_t slap_tflm_arena[SLAP_ARENA_BYTES];
static SlapTflm slap_tflm_default(g_model, SLAP_WINDOW, SLAP_N_CLASSES, SLAP_CLASS_IDS, SLAP_MEAN,
                                  SLAP_STD, slap_tflm_arena, SLAP_ARENA_BYTES);
inline size_t slapTflmBegin() { return slap_tflm_default.begin(); }
inline void slapTflmClassify(const float* win, uint8_t* cls, uint8_t* conf) {
  slap_tflm_default.classify(win, cls, conf);
}
#endif
