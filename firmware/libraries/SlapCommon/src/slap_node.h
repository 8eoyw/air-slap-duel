// Air Slap Duel — wearable node runtime (Nano 33 BLE Sense).
// IMU ring buffer -> threshold trigger -> classifier -> BLE event.
// Header-only; include from exactly one sketch. The sketch supplies a classifier:
//   slap_classifier_tflm.h       (trained model.h present)
//   slap_classifier_heuristic.h  (no model yet: sign-of-axis rules, for integration tests)
//
// Serial log lines (parsed by tools/analyze_log.py):
//   INFO,<key>=<value>...
//   TRIG,<t_local_ms>
//   EVT,<t_local_ms>,<t_ref_ms>,<cls>,<conf>,<peak_dps>,<infer_us>
//   REJ,<t_local_ms>,<cls>,<conf>,<infer_us>          (background or low confidence)
//   SYNC,<offset_ms>,<median_offset_ms>
#pragma once
#include <Arduino.h>
#include <ArduinoBLE.h>
#include "slap_board.h"
#include "slap_protocol.h"

#ifndef SLAP_WINDOW
#define SLAP_WINDOW 100          // samples per inference window (1 s @ 100 Hz)
#endif
#ifndef SLAP_PRE
#define SLAP_PRE (SLAP_WINDOW / 2)  // samples kept before the trigger
#endif

// Classifier contract: `win` is SLAP_WINDOW x 6 floats, oldest first,
// channels ax,ay,az (g), gx,gy,gz (dps). Returns SlapClass id and confidence 0..255.
typedef void (*SlapClassifyFn)(const float* win, uint8_t* cls, uint8_t* conf);

struct SlapNodeConfig {
  SlapRole    role;
  const char* name;
  float       acc_trigger_g;     // |a| above this opens a window (includes 1 g gravity)
  float       gyro_trigger_dps;  // or |g| above this
  uint8_t     conf_min;          // below -> treated as background
  uint16_t    cooldown_ms;       // after a trigger, ignore new triggers this long
};

class SlapNode {
 public:
  SlapNode(const SlapNodeConfig& cfg, SlapClassifyFn fn)
      : cfg_(cfg), classify_(fn),
        service_(SLAP_SERVICE_UUID),
        eventChar_(SLAP_EVENT_UUID, BLENotify, sizeof(SlapEvent)),
        syncChar_(SLAP_SYNC_UUID, BLEWriteWithoutResponse | BLEWrite, sizeof(SlapSync)),
        roleChar_(SLAP_ROLE_UUID, BLERead),
        rawChar_(SLAP_RAW_UUID, BLENotify, sizeof(SlapRawSample)) {
    self_ = this;
  }

  bool begin() {
    pinMode(LED_BUILTIN, OUTPUT);
#ifdef LEDR
    pinMode(LEDR, OUTPUT); pinMode(LEDG, OUTPUT); pinMode(LEDB, OUTPUT);
    rgb(1, 0, 0);
#endif
    if (!IMU.begin()) { Serial.println("INFO,error=imu_init"); return false; }
    if (!BLE.begin()) { Serial.println("INFO,error=ble_init"); return false; }

    BLE.setLocalName(cfg_.name);
    BLE.setDeviceName(cfg_.name);
    BLE.setAdvertisedService(service_);
    service_.addCharacteristic(eventChar_);
    service_.addCharacteristic(syncChar_);
    service_.addCharacteristic(roleChar_);
    service_.addCharacteristic(rawChar_);
    BLE.addService(service_);
    roleChar_.writeValue((uint8_t)cfg_.role);
    syncChar_.setEventHandler(BLEWritten, onSyncThunk);
    BLE.setEventHandler(BLEConnected, onConnThunk);
    BLE.setEventHandler(BLEDisconnected, onDiscThunk);
    // 7.5–15 ms connection interval (units of 1.25 ms); the central has the final say.
    BLE.setConnectionInterval(6, 12);
    BLE.advertise();

    Serial.print("INFO,name="); Serial.print(cfg_.name);
    Serial.print(",role="); Serial.print(cfg_.role);
    Serial.print(",window="); Serial.print(SLAP_WINDOW);
    Serial.print(",hz="); Serial.println(SAMPLE_HZ);
    nextUs_ = micros();
    return true;
  }

  // Set by the classifier after init, printed for experiment 2.
  void reportArena(size_t used) {
    Serial.print("INFO,arena_used="); Serial.println((unsigned)used);
  }

  void loop() {
    BLE.poll();
    unsigned long now = micros();
    if ((long)(now - nextUs_) < 0) return;
    nextUs_ += 1000000UL / SAMPLE_HZ;
    if ((long)(now - nextUs_) > 100000L) nextUs_ = now;  // fell far behind (e.g. BLE stall)

    float s[6];
    if (!readImu(s)) return;
    push(s);

#ifdef SLAP_STREAM_RAW
    streamRaw(s);
    return;
#endif

    uint32_t ms = millis();
    if (!collecting_) {
      float a = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
      float g = sqrtf(s[3] * s[3] + s[4] * s[4] + s[5] * s[5]);
      bool cool = (uint32_t)(ms - lastTrig_) < cfg_.cooldown_ms;
      if (!cool && (a > cfg_.acc_trigger_g || g > cfg_.gyro_trigger_dps)) {
        collecting_ = true;
        remaining_ = SLAP_WINDOW - SLAP_PRE;
        lastTrig_ = ms;
        onset_ = ms;
        Serial.print("TRIG,"); Serial.println(ms);
      }
    }
    if (collecting_ && --remaining_ <= 0) {
      collecting_ = false;
      infer();
    }
  }

 private:
  static constexpr int RING = SLAP_WINDOW;

  bool readImu(float* s) {
    if (!IMU.accelerationAvailable() || !IMU.gyroscopeAvailable()) {
      // Reuse the previous sample so the timebase stays fixed.
      if (count_ == 0) return false;
      memcpy(s, ring_[(head_ + RING - 1) % RING], sizeof(float) * 6);
      return true;
    }
    IMU.readAcceleration(s[0], s[1], s[2]);
    IMU.readGyroscope(s[3], s[4], s[5]);
    return true;
  }

  void push(const float* s) {
    memcpy(ring_[head_], s, sizeof(float) * 6);
    head_ = (head_ + 1) % RING;
    if (count_ < RING) count_++;
  }

  void infer() {
    if (count_ < RING) return;
    static float win[SLAP_WINDOW * 6];
    uint16_t peak = 0;
    for (int i = 0; i < RING; i++) {
      const float* s = ring_[(head_ + i) % RING];
      memcpy(&win[i * 6], s, sizeof(float) * 6);
      float g = sqrtf(s[3] * s[3] + s[4] * s[4] + s[5] * s[5]);
      if (g > peak) peak = (uint16_t)min(g, 65535.0f);
    }
    uint8_t cls = CLS_BACKGROUND, conf = 0;
    unsigned long t0 = micros();
    classify_(win, &cls, &conf);
    unsigned long us = micros() - t0;

    if (cls == CLS_BACKGROUND || conf < cfg_.conf_min) {
      Serial.print("REJ,"); Serial.print(onset_); Serial.print(',');
      Serial.print(cls); Serial.print(','); Serial.print(conf); Serial.print(',');
      Serial.println(us);
      return;
    }
    SlapEvent e;
    e.t_onset = onset_ + medianOffset();
    e.cls = cls;
    e.conf = conf;
    e.peak = peak;
    eventChar_.writeValue(&e, sizeof(e));
    digitalWrite(LED_BUILTIN, HIGH);
    ledOffAt_ = millis() + 150;

    Serial.print("EVT,"); Serial.print(onset_); Serial.print(',');
    Serial.print(e.t_onset); Serial.print(','); Serial.print(slapClassName(cls));
    Serial.print(','); Serial.print(conf); Serial.print(','); Serial.print(peak);
    Serial.print(','); Serial.println(us);
  }

  void streamRaw(const float* s) {
    SlapRawSample r;
    r.seq = rawSeq_++;
    for (int i = 0; i < 3; i++) {
      r.a[i] = (int16_t)constrain(s[i] * 1000.0f, -32768.0f, 32767.0f);
      r.g[i] = (int16_t)constrain(s[3 + i] * 10.0f, -32768.0f, 32767.0f);
    }
    if (BLE.connected()) rawChar_.writeValue(&r, sizeof(r));
  }

  int32_t medianOffset() const {
    if (nOffsets_ == 0) return 0;
    int32_t tmp[8];
    int n = nOffsets_;
    memcpy(tmp, offsets_, sizeof(int32_t) * n);
    for (int i = 1; i < n; i++) {  // insertion sort, n <= 8
      int32_t v = tmp[i]; int j = i - 1;
      while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
      tmp[j + 1] = v;
    }
    return tmp[n / 2];
  }

  void onSync(BLECharacteristic& c) {
    SlapSync s;
    if (c.valueLength() < (int)sizeof(s)) return;
    memcpy(&s, c.value(), sizeof(s));
    int32_t off = (int32_t)(s.referee_ms - millis());
    offsets_[offIdx_] = off;
    offIdx_ = (offIdx_ + 1) % 8;
    if (nOffsets_ < 8) nOffsets_++;
    Serial.print("SYNC,"); Serial.print(off); Serial.print(','); Serial.println(medianOffset());
  }

  void rgb(bool r, bool g, bool b) {
#ifdef LEDR
    digitalWrite(LEDR, r ? LOW : HIGH);  // active low
    digitalWrite(LEDG, g ? LOW : HIGH);
    digitalWrite(LEDB, b ? LOW : HIGH);
#endif
  }

  static void onSyncThunk(BLEDevice, BLECharacteristic c) { self_->onSync(c); }
  static void onConnThunk(BLEDevice d) {
    self_->rgb(0, 1, 0);
    self_->nOffsets_ = 0;  // new central, new clock
    Serial.print("INFO,connected="); Serial.println(d.address());
  }
  static void onDiscThunk(BLEDevice) {
    self_->rgb(1, 0, 0);
    Serial.println("INFO,disconnected=1");
    BLE.advertise();
  }

 public:
  // Call from loop() too; turns the event LED off without blocking.
  void service() {
    if (ledOffAt_ && (int32_t)(millis() - ledOffAt_) >= 0) {
      digitalWrite(LED_BUILTIN, LOW);
      ledOffAt_ = 0;
    }
  }

 private:
  SlapNodeConfig cfg_;
  SlapClassifyFn classify_;
  BLEService service_;
  BLECharacteristic eventChar_, syncChar_;
  BLEByteCharacteristic roleChar_;
  BLECharacteristic rawChar_;

  float ring_[RING][6];
  int head_ = 0, count_ = 0;
  bool collecting_ = false;
  int remaining_ = 0;
  uint32_t lastTrig_ = 0, onset_ = 0, ledOffAt_ = 0;
  unsigned long nextUs_ = 0;
  uint16_t rawSeq_ = 0;

  int32_t offsets_[8];
  int nOffsets_ = 0, offIdx_ = 0;

  static SlapNode* self_;
};

SlapNode* SlapNode::self_ = nullptr;
