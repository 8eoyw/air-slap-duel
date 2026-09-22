// Air Slap Duel — shared BLE protocol (Nano peripherals <-> XiaoZhi referee)
// See docs/protocol.md
#pragma once
#include <stdint.h>

#define SLAP_SERVICE_UUID "5a1a0000-8c3b-4f7e-9d2a-000000000001"
#define SLAP_EVENT_UUID   "5a1a0001-8c3b-4f7e-9d2a-000000000001"  // notify
#define SLAP_SYNC_UUID    "5a1a0002-8c3b-4f7e-9d2a-000000000001"  // write w/o response
#define SLAP_ROLE_UUID    "5a1a0003-8c3b-4f7e-9d2a-000000000001"  // read

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
#pragma pack(pop)

static_assert(sizeof(SlapEvent) == 8, "SlapEvent must stay 8 bytes");
