// Air Slap Duel — shared BLE protocol (Nano peripherals <-> referee)
// See docs/protocol.md. Plain C++, no Arduino dependency, so the referee
// core, the host tests and the ESP-IDF component can include it too.
#pragma once
#include <stdint.h>

#define SLAP_SERVICE_UUID "5a1a0000-8c3b-4f7e-9d2a-000000000001"
#define SLAP_EVENT_UUID   "5a1a0001-8c3b-4f7e-9d2a-000000000001"  // notify
#define SLAP_SYNC_UUID    "5a1a0002-8c3b-4f7e-9d2a-000000000001"  // write w/o response
#define SLAP_ROLE_UUID    "5a1a0003-8c3b-4f7e-9d2a-000000000001"  // read
#define SLAP_RAW_UUID     "5a1a0004-8c3b-4f7e-9d2a-000000000001"  // notify, experiment 3 only

#define SLAP_NAME_ATTACKER "SLAP-ATK"
#define SLAP_NAME_DEFENDER "SLAP-DEF"

enum SlapRole : uint8_t { ROLE_ATTACKER = 0, ROLE_DEFENDER = 1 };

// Class ids. Attacker and defender share one id space so the referee
// never has to know which model produced an event.
enum SlapClass : uint8_t {
  CLS_BACKGROUND  = 0,
  CLS_FOREHAND    = 1,
  CLS_BACKHAND    = 2,
  CLS_FEINT       = 3,
  CLS_DODGE_LEFT  = 10,
  CLS_DODGE_RIGHT = 11,
  CLS_BLOCK       = 12,
};

inline const char* slapClassName(uint8_t c) {
  switch (c) {
    case CLS_BACKGROUND:  return "background";
    case CLS_FOREHAND:    return "forehand";
    case CLS_BACKHAND:    return "backhand";
    case CLS_FEINT:       return "feint";
    case CLS_DODGE_LEFT:  return "dodge_left";
    case CLS_DODGE_RIGHT: return "dodge_right";
    case CLS_BLOCK:       return "block";
    default:              return "unknown";
  }
}

inline bool slapIsAttack(uint8_t c) { return c >= CLS_FOREHAND && c <= CLS_FEINT; }
inline bool slapIsDefense(uint8_t c) { return c >= CLS_DODGE_LEFT && c <= CLS_BLOCK; }

#pragma pack(push, 1)
struct SlapEvent {
  uint32_t t_onset;  // ms, already converted to the referee clock
  uint8_t  cls;      // SlapClass
  uint8_t  conf;     // confidence * 255
  uint16_t peak;     // peak |gyro| in dps, used for strength tiers
};
struct SlapSync {
  uint32_t referee_ms;
};
// One raw IMU sample, int16 fixed point: accel in mg, gyro in 0.1 dps.
// 14 bytes fits the default 20-byte ATT payload.
struct SlapRawSample {
  uint16_t seq;
  int16_t  a[3];
  int16_t  g[3];
};
#pragma pack(pop)

static_assert(sizeof(SlapEvent) == 8, "SlapEvent must stay 8 bytes");
static_assert(sizeof(SlapRawSample) == 14, "SlapRawSample must stay 14 bytes");
