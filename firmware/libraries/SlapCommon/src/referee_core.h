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

// Append only: the names travel over UART/JSON and are parsed back by index range.
enum RefOutcome : uint8_t {
  OUT_HIT,           // no/late defense
  OUT_JUMPED,        // defender moved too early -> hit
  OUT_GRAZE,         // wrong direction -> half damage
  OUT_MISS,          // clean dodge, no counter
  OUT_FEINT_FOOLED,  // defender reacted to a feint, no follow-up -> next window shrinks
  OUT_FEINT_HELD,    // unused since rules v2 (a held feint keeps the attack phase going)
  OUT_BLOCKED,       // block -> half damage, no counter
  OUT_COUNTERED,     // clean dodge + slap back -> attacker takes counter_damage
  OUT_FEINT_PUNISHED,// defender bit on a feint and ate the follow-up slap -> crit
  OUT_TIMEOUT,       // attack phase ran out without a real slap -> turn passes
  OUT_COUNT_
};

enum RefStrength : uint8_t { STR_LIGHT, STR_MEDIUM, STR_HEAVY };

struct RoundResult {
  RefOutcome  outcome;
  RefStrength strength;        // of the slap that was judged (the follow-up after a feint)
  uint8_t     attack_cls;
  uint8_t     defense_cls;     // CLS_BACKGROUND if none
  int32_t     delta_ms;        // t_defense - t_attack, 0 if no defense
  uint8_t     damage;          // taken by the defender
  uint8_t     attacker;        // player index 0/1
  int16_t     hp[2];
  uint8_t     counter_damage;  // taken by the attacker (OUT_COUNTERED only)
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
  explicit RefereeCore(RefereeOutput* out) : out_(out), left_{false, false} { reset(); }

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

  // Player boards (SLAP-P1 / SLAP-P2, firmware/nano_player): each player keeps one
  // board that reports both attack and defense classes. Routes by who attacks now.
  void onPlayerEvent(uint8_t player, const SlapEvent& e, uint32_t now);
  // true: players keep their boards, so the SWAP pause is skipped.
  void setPlayerBoards(bool on) { player_boards_ = on; }
  bool playerBoards() const { return player_boards_; }

  // Mirrors the forehand/backhand -> dodge mapping when this player attacks.
  void setLeftHanded(uint8_t player, bool left) { left_[player & 1] = left; }

  // Call often (every loop / every 10 ms).
  void tick(uint32_t now);

  RefState state() const { return state_; }
  int16_t hp(uint8_t player) const { return hp_[player & 1]; }
  uint8_t attacker() const { return attacker_; }
  uint8_t round() const { return round_; }

  // Pure judging function, exposed for tests.
  // `atk` must be a real slap (forehand/backhand); feints are handled by the state machine.
  static RoundResult judge(const SlapEvent& atk, const SlapEvent* def, bool feinted_before,
                           bool attacker_left_handed = false);
  static RefStrength strengthOf(uint16_t peak);
  static uint8_t damageFor(RefStrength s);
  // Damage the attacker takes from a counter slap after `dodged` was dodged.
  static uint8_t counterDamage(const SlapEvent& dodged, const SlapEvent& counter);

 private:
  void enter(RefState s, uint32_t now);
  void arm(uint32_t now);
  void resolve(uint32_t now);
  void finish(RoundResult r, uint32_t now);
  uint32_t defenseWindow() const;

  RefereeOutput* out_;
  RefState state_;
  uint32_t entered_ms_;
  int16_t  hp_[2];
  uint8_t  attacker_;
  uint8_t  round_;
  bool     feinted_[2];     // per player: was fooled by a feint last time they defended
  bool     left_[2];        // per player: left-handed
  bool     player_boards_ = false;
  uint32_t attack_deadline_;  // end of the attack phase (survives a held feint)

  SlapEvent atk_;
  bool      have_def_;
  SlapEvent def_;
  // Last defender event seen before the attack arrived (to catch jumps).
  bool      have_early_def_;
  SlapEvent early_def_;
  // Clean dodge -> counter slap from the defender.
  bool      have_counter_;
  SlapEvent counter_;
  // Feint the defender bit on -> follow-up slap from the attacker.
  bool      have_follow_;
  SlapEvent follow_;
};
