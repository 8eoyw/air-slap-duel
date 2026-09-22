// Portable referee: round state machine + hit judging.
// No platform code. The host feeds events and time; the core calls back into
// a RefereeOutput for everything the players see or hear.
// Used by: tests/ (host g++), firmware/nano_hub (Arduino), firmware/referee_xiaozhi (ESP-IDF).
#pragma once
#include <stdint.h>
#include "slap_protocol.h"
#include "slap_game_config.h"

enum RefState : uint8_t {
  REF_IDLE, REF_ANNOUNCE, REF_ARMED, REF_JUDGING, REF_RESOLVE, REF_SWAP, REF_GAME_OVER
};

enum RefOutcome : uint8_t {
  OUT_HIT,         // no/late defense
  OUT_JUMPED,      // defender moved too early -> hit
  OUT_GRAZE,       // wrong direction -> half damage
  OUT_MISS,        // clean dodge or block
  OUT_FEINT_FOOLED,// defender reacted to a feint -> next window shrinks
  OUT_FEINT_HELD,  // defender stayed still on a feint
};

enum RefStrength : uint8_t { STR_LIGHT, STR_MEDIUM, STR_HEAVY };

struct RoundResult {
  RefOutcome  outcome;
  RefStrength strength;
  uint8_t     attack_cls;
  uint8_t     defense_cls;   // CLS_BACKGROUND if none
  int32_t     delta_ms;      // t_defense - t_attack, 0 if no defense
  uint8_t     damage;
  uint8_t     attacker;      // player index 0/1
  int16_t     hp[2];
};

// Everything user-visible. Implement on each platform; every method is optional.
class RefereeOutput {
 public:
  virtual ~RefereeOutput() {}
  virtual void onState(RefState) {}
  virtual void onAnnounce(uint8_t /*attacker*/, uint8_t /*round*/) {}
  virtual void onResult(const RoundResult&) {}
  virtual void onSwap(uint8_t /*next_attacker*/) {}
  virtual void onGameOver(uint8_t /*winner*/, const int16_t /*hp*/[2]) {}
};

const char* refStateName(RefState s);
const char* refOutcomeName(RefOutcome o);

class RefereeCore {
 public:
  explicit RefereeCore(RefereeOutput* out) : out_(out) { reset(); }

  void reset();
  // Voice "start" / button. Starts a new game from IDLE or GAME_OVER.
  void start(uint32_t now);
  // Platform signals the announcement (TTS) has finished; optional.
  void announceDone(uint32_t now);
  // Players confirm the device swap; optional (otherwise SWAP_TIMEOUT_MS).
  void swapDone(uint32_t now);

  // Events from the attacker / defender device. Times are referee clock ms.
  void onAttackerEvent(const SlapEvent& e, uint32_t now);
  void onDefenderEvent(const SlapEvent& e, uint32_t now);

  // Call often (every loop / every 10 ms).
  void tick(uint32_t now);

  RefState state() const { return state_; }
  int16_t hp(uint8_t player) const { return hp_[player & 1]; }
  uint8_t attacker() const { return attacker_; }
  uint8_t round() const { return round_; }

  // Pure judging function, exposed for tests.
  static RoundResult judge(const SlapEvent& atk, const SlapEvent* def, bool feinted_before);
  static RefStrength strengthOf(uint16_t peak);

 private:
  void enter(RefState s, uint32_t now);
  void resolve(uint32_t now);

  RefereeOutput* out_;
  RefState state_;
  uint32_t entered_ms_;
  int16_t  hp_[2];
  uint8_t  attacker_;
  uint8_t  round_;
  bool     feinted_[2];     // per player: was fooled by a feint last time they defended

  SlapEvent atk_;
  bool      have_def_;
  SlapEvent def_;
  // Last defender event seen before the attack arrived (to catch jumps).
  bool      have_early_def_;
  SlapEvent early_def_;
};
