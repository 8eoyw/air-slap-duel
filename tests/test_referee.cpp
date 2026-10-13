// Host tests for the portable referee core.
//   make -C tests      (or see tests/Makefile for the g++ line)
#include <cstdio>
#include <vector>
#include "referee_core.h"

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
      failures++;                                                      \
    }                                                                  \
  } while (0)

static SlapEvent ev(uint32_t t, uint8_t cls, uint16_t peak = 500) {
  SlapEvent e;
  e.t_onset = t;
  e.cls = cls;
  e.conf = 230;
  e.peak = peak;
  return e;
}

struct Recorder : RefereeOutput {
  std::vector<RoundResult> results;
  std::vector<RefState> states;
  int gameOverWinner = -1;
  void onState(RefState s) override { states.push_back(s); }
  void onResult(const RoundResult& r) override { results.push_back(r); }
  void onGameOver(uint8_t w, const int16_t*) override { gameOverWinner = w; }
};

static void test_judge_table() {
  std::puts("judge table");
  SlapEvent atk = ev(1000, CLS_FOREHAND, 500);  // medium -> 20
  SlapEvent d;

  RoundResult r = RefereeCore::judge(atk, nullptr, false);
  CHECK(r.outcome == OUT_HIT && r.damage == DMG_MEDIUM);

  d = ev(1300, DODGE_FOR_FOREHAND);
  r = RefereeCore::judge(atk, &d, false);
  CHECK(r.outcome == OUT_MISS && r.damage == 0 && r.delta_ms == 300);

  d = ev(1300, CLS_BLOCK);  // block: always stops it, but only halves it
  r = RefereeCore::judge(atk, &d, false);
  CHECK(r.outcome == OUT_BLOCKED && r.damage == DMG_MEDIUM / 2);

  d = ev(1300, DODGE_FOR_BACKHAND);  // left-handed forehand is mirrored
  CHECK(RefereeCore::judge(atk, &d, false, true).outcome == OUT_MISS);
  d = ev(1300, DODGE_FOR_FOREHAND);
  CHECK(RefereeCore::judge(atk, &d, false, true).outcome == OUT_GRAZE);

  d = ev(1300, DODGE_FOR_BACKHAND);  // wrong way
  r = RefereeCore::judge(atk, &d, false);
  CHECK(r.outcome == OUT_GRAZE && r.damage == DMG_MEDIUM / 2);

  d = ev(1100, DODGE_FOR_FOREHAND);  // 100 ms: too early
  CHECK(RefereeCore::judge(atk, &d, false).outcome == OUT_JUMPED);

  d = ev(900, DODGE_FOR_FOREHAND);   // before the attack
  CHECK(RefereeCore::judge(atk, &d, false).outcome == OUT_JUMPED);

  d = ev(1700, DODGE_FOR_FOREHAND);  // 700 ms: too slow
  CHECK(RefereeCore::judge(atk, &d, false).outcome == OUT_HIT);

  d = ev(1500, DODGE_FOR_FOREHAND);  // 500 ms: ok normally, too slow after a feint
  CHECK(RefereeCore::judge(atk, &d, false).outcome == OUT_MISS);
  CHECK(RefereeCore::judge(atk, &d, true).outcome == OUT_HIT);

  // Counter damage: x3/2, or x2 when the dodged slap was light.
  CHECK(RefereeCore::counterDamage(ev(0, CLS_FOREHAND, 500), ev(0, CLS_BACKHAND, 500)) ==
        DMG_MEDIUM * 3 / 2);
  CHECK(RefereeCore::counterDamage(ev(0, CLS_FOREHAND, 100), ev(0, CLS_BACKHAND, 500)) ==
        DMG_MEDIUM * 2);
}

static void test_strength() {
  std::puts("strength tiers");
  CHECK(RefereeCore::strengthOf(PEAK_MEDIUM_DPS - 1) == STR_LIGHT);
  CHECK(RefereeCore::strengthOf(PEAK_MEDIUM_DPS) == STR_MEDIUM);
  CHECK(RefereeCore::strengthOf(PEAK_HEAVY_DPS) == STR_HEAVY);
}

// Drive a full round through the state machine.
static void play_round(RefereeCore& ref, uint32_t& now, uint8_t atkCls, int defDelta,
                       uint8_t defCls, uint16_t peak) {
  ref.announceDone(now);
  now += 100;
  ref.onAttackerEvent(ev(now, atkCls, peak), now + 30);  // 30 ms BLE latency
  uint32_t atkT = now;
  if (defDelta != INT32_MIN) ref.onDefenderEvent(ev(atkT + defDelta, defCls), atkT + defDelta + 30);
  for (int i = 0; i < 350; i++) {  // 3.5 s of ticks: window + grace + RESOLVE_SHOW_MS
    now += 10;
    ref.tick(now);
  }
  ref.swapDone(now);
}

static void test_state_machine() {
  std::puts("state machine");
  Recorder rec;
  RefereeCore ref(&rec);
  uint32_t now = 0;

  // Events before start are ignored.
  ref.onAttackerEvent(ev(10, CLS_FOREHAND), 10);
  CHECK(ref.state() == REF_IDLE);

  ref.start(now);
  CHECK(ref.state() == REF_ANNOUNCE && ref.round() == 1);
  ref.tick(now + ANNOUNCE_TIMEOUT_MS);
  CHECK(ref.state() == REF_ARMED);
  now += ANNOUNCE_TIMEOUT_MS;

  // Background events never start a round.
  ref.onAttackerEvent(ev(now, CLS_BACKGROUND), now);
  CHECK(ref.state() == REF_ARMED);

  // No attack for too long -> the turn passes, no damage.
  ref.tick(now + ATTACK_TIMEOUT_MS);
  CHECK(ref.state() == REF_RESOLVE && rec.results.size() == 1);
  CHECK(rec.results[0].outcome == OUT_TIMEOUT && rec.results[0].damage == 0);
  now += ATTACK_TIMEOUT_MS + RESOLVE_SHOW_MS;
  ref.tick(now);
  CHECK(ref.state() == REF_SWAP && ref.attacker() == 1);
  ref.swapDone(now);
  // Give the attack back to player 0 the same way, so the rounds below start from p0.
  ref.announceDone(now);
  ref.tick(now += ATTACK_TIMEOUT_MS);
  ref.tick(now += RESOLVE_SHOW_MS);
  ref.swapDone(now);
  CHECK(ref.attacker() == 0 && ref.round() == 3);
  rec.results.clear();

  // Round 1: player 0 hits heavy, no defense.
  play_round(ref, now, CLS_FOREHAND, INT32_MIN, 0, PEAK_HEAVY_DPS);
  CHECK(rec.results.size() == 1);
  CHECK(rec.results[0].outcome == OUT_HIT && rec.results[0].attacker == 0);
  CHECK(ref.hp(1) == HP_START - DMG_HEAVY && ref.hp(0) == HP_START);
  CHECK(ref.attacker() == 1);  // swapped
  CHECK(ref.state() == REF_ANNOUNCE && ref.round() == 4);

  // Round 2: player 1 attacks, player 0 dodges cleanly.
  play_round(ref, now, CLS_BACKHAND, 350, DODGE_FOR_BACKHAND, 500);
  CHECK(rec.results.size() == 2 && rec.results[1].outcome == OUT_MISS);
  CHECK(ref.hp(0) == HP_START);

  // Round 3: defender dodges before the attack arrives -> jumped.
  ref.announceDone(now);
  now += 100;
  ref.onDefenderEvent(ev(now, DODGE_FOR_FOREHAND), now + 20);
  now += 150;
  ref.onAttackerEvent(ev(now, CLS_FOREHAND, 100), now + 20);
  for (int i = 0; i < 350; i++) ref.tick(now += 10);
  CHECK(rec.results.size() == 3 && rec.results[2].outcome == OUT_JUMPED);
  CHECK(ref.hp(1) == HP_START - DMG_HEAVY - DMG_LIGHT);
  ref.swapDone(now);

  // Only the first defense counts.
  ref.announceDone(now);
  now += 100;
  uint32_t t = now;
  ref.onAttackerEvent(ev(t, CLS_FOREHAND, 100), t);
  ref.onDefenderEvent(ev(t + 300, DODGE_FOR_BACKHAND), t + 300);  // wrong way first
  ref.onDefenderEvent(ev(t + 350, DODGE_FOR_FOREHAND), t + 350);
  for (int i = 0; i < 350; i++) ref.tick(now += 10);
  CHECK(rec.results.size() == 4 && rec.results[3].outcome == OUT_GRAZE);
  ref.swapDone(now);
}

static void test_game_over() {
  std::puts("game over");
  Recorder rec;
  RefereeCore ref(&rec);
  uint32_t now = 0;
  ref.start(now);
  // Player 0 hits heavy every time they attack; player 1's heavy slaps get blocked (15 each).
  for (int i = 0; i < 20 && ref.state() != REF_GAME_OVER; i++) {
    if (ref.attacker() == 0)
      play_round(ref, now, CLS_FOREHAND, INT32_MIN, 0, PEAK_HEAVY_DPS);
    else
      play_round(ref, now, CLS_FOREHAND, 300, CLS_BLOCK, PEAK_HEAVY_DPS);
  }
  CHECK(ref.hp(0) == HP_START - 3 * (DMG_HEAVY / 2));
  CHECK(ref.state() == REF_GAME_OVER);
  CHECK(rec.gameOverWinner == 0);
  CHECK(ref.hp(1) == 0);
  // 100 HP / 30 per heavy hit = 4 hits, so 7 rounds (4 attacks by p0, 3 by p1).
  CHECK(rec.results.size() == 7);

  ref.start(now);  // rematch
  CHECK(ref.state() == REF_ANNOUNCE && ref.hp(0) == HP_START && ref.hp(1) == HP_START);
}

static void test_feint_penalty_lasts_one_round() {
  std::puts("feint penalty");
  Recorder rec;
  RefereeCore ref(&rec);
  uint32_t now = 0;
  ref.start(now);
  // p0 feints, p1 falls for it.
  play_round(ref, now, CLS_FEINT, 300, CLS_DODGE_LEFT, 500);
  CHECK(rec.results.back().outcome == OUT_FEINT_FOOLED);
  // p1 attacks (their own penalty doesn't apply when attacking).
  play_round(ref, now, CLS_FOREHAND, 300, CLS_BLOCK, 500);
  // p0 attacks again; p1 answers at 500 ms, which is too slow under the penalty.
  play_round(ref, now, CLS_FOREHAND, 500, DODGE_FOR_FOREHAND, 500);
  CHECK(rec.results.back().outcome == OUT_HIT);
  play_round(ref, now, CLS_FOREHAND, 300, CLS_BLOCK, 500);
  // Penalty is gone now.
  play_round(ref, now, CLS_FOREHAND, 500, DODGE_FOR_FOREHAND, 500);
  CHECK(rec.results.back().outcome == OUT_MISS);
}

static void tick_for(RefereeCore& ref, uint32_t& now, int ms) {
  for (int i = 0; i < ms / 10; i++) ref.tick(now += 10);
}

static void test_counter() {
  std::puts("counter");
  Recorder rec;
  RefereeCore ref(&rec);
  ref.setPlayerBoards(true);
  uint32_t now = 0;
  ref.start(now);
  ref.announceDone(now);
  uint32_t t = now + 100;
  // p0 slaps light, p1 dodges right way, then slaps back heavy 400 ms later.
  // Board events route by player; p1's dodge is followed by its own counter slap.
  ref.onPlayerEvent(0, ev(t, CLS_FOREHAND, 100), t + 500);
  ref.onPlayerEvent(1, ev(t + 300, DODGE_FOR_FOREHAND), t + 800);
  tick_for(ref, now, 900);
  CHECK(ref.state() == REF_JUDGING);  // still waiting for a counter
  ref.onPlayerEvent(1, ev(t + 700, CLS_BACKHAND, PEAK_HEAVY_DPS), t + 1200);
  tick_for(ref, now, 50);
  CHECK(rec.results.size() == 1 && rec.results[0].outcome == OUT_COUNTERED);
  CHECK(rec.results[0].damage == 0 && rec.results[0].counter_damage == DMG_HEAVY * 2);
  CHECK(ref.hp(0) == HP_START - DMG_HEAVY * 2 && ref.hp(1) == HP_START);
  // Player boards: no SWAP pause.
  tick_for(ref, now, RESOLVE_SHOW_MS);
  CHECK(ref.state() == REF_ANNOUNCE && ref.attacker() == 1);

  // No counter in time -> plain MISS after the counter window.
  ref.announceDone(now);
  t = now + 100;
  ref.onPlayerEvent(1, ev(t, CLS_BACKHAND, 500), t + 500);
  ref.onPlayerEvent(0, ev(t + 300, DODGE_FOR_BACKHAND), t + 800);
  ref.onPlayerEvent(0, ev(t + 300 + COUNTER_WINDOW_MS + 50, CLS_FOREHAND, 500), t + 1700);  // too late
  tick_for(ref, now, 2000);
  CHECK(rec.results.size() == 2 && rec.results[1].outcome == OUT_MISS);
  CHECK(ref.hp(1) == HP_START);
  tick_for(ref, now, RESOLVE_SHOW_MS);

  // Block never opens a counter.
  ref.announceDone(now);
  t = now + 100;
  ref.onPlayerEvent(0, ev(t, CLS_FOREHAND, 500), t + 500);
  ref.onPlayerEvent(1, ev(t + 300, CLS_BLOCK), t + 800);
  ref.onPlayerEvent(1, ev(t + 600, CLS_BACKHAND, 500), t + 1100);
  tick_for(ref, now, 2000);
  CHECK(rec.results.size() == 3 && rec.results[2].outcome == OUT_BLOCKED);
  CHECK(rec.results[2].counter_damage == 0 && ref.hp(0) == HP_START - DMG_HEAVY * 2);
}

static void test_feint_rules() {
  std::puts("feint rules");
  Recorder rec;
  RefereeCore ref(&rec);
  ref.setPlayerBoards(true);
  uint32_t now = 0;
  ref.start(now);
  ref.announceDone(now);

  // Held feint: back to ARMED, no result, deadline unchanged.
  uint32_t t = now + 100;
  ref.onAttackerEvent(ev(t, CLS_FEINT), t + 500);
  CHECK(ref.state() == REF_JUDGING);
  tick_for(ref, now, 100 + DODGE_WINDOW_MAX_MS + BLE_GRACE_MS + 100);
  CHECK(ref.state() == REF_ARMED && rec.results.empty());

  // Bitten feint + follow-up after the reaction -> undodgeable crit.
  t = now + 100;
  ref.onAttackerEvent(ev(t, CLS_FEINT), t + 500);
  ref.onDefenderEvent(ev(t + 250, CLS_DODGE_LEFT), t + 750);
  ref.onAttackerEvent(ev(t + 600, CLS_FOREHAND, 500), t + 1100);
  tick_for(ref, now, 1200);
  CHECK(rec.results.size() == 1 && rec.results[0].outcome == OUT_FEINT_PUNISHED);
  CHECK(rec.results[0].damage == DMG_MEDIUM * 3 / 2 && ref.hp(1) == HP_START - DMG_MEDIUM * 3 / 2);
  tick_for(ref, now, RESOLVE_SHOW_MS);
  CHECK(ref.attacker() == 1);

  // The whole attack phase is bounded: feints can't stall past the deadline.
  ref.announceDone(now);
  for (int i = 0; i < 10 && rec.results.size() < 2; i++) {  // keep feinting until the phase ends
    t = now + 100;
    ref.onAttackerEvent(ev(t, CLS_FEINT), t + 500);
    tick_for(ref, now, 100 + DODGE_WINDOW_MAX_MS + BLE_GRACE_MS + 100);  // until it re-arms
  }
  CHECK(rec.results.size() == 2 && rec.results[1].outcome == OUT_TIMEOUT);
  tick_for(ref, now, RESOLVE_SHOW_MS);

  // Bitten feint, no follow-up -> FEINT_FOOLED, no damage.
  ref.announceDone(now);
  t = now + 100;
  ref.onAttackerEvent(ev(t, CLS_FEINT), t + 500);
  ref.onDefenderEvent(ev(t + 250, CLS_BLOCK), t + 750);
  tick_for(ref, now, 350 + FOLLOWUP_WINDOW_MS + BLE_GRACE_MS + 100);
  CHECK(rec.results.size() == 3 && rec.results[2].outcome == OUT_FEINT_FOOLED);
  CHECK(rec.results[2].damage == 0);
  tick_for(ref, now, RESOLVE_SHOW_MS);

  // Real slap before the defender reacts to the feint: judged as a normal slap.
  ref.announceDone(now);
  t = now + 100;
  ref.onAttackerEvent(ev(t, CLS_FEINT), t + 500);
  ref.onAttackerEvent(ev(t + 200, CLS_FOREHAND, 500), t + 700);
  ref.onDefenderEvent(ev(t + 500, DODGE_FOR_FOREHAND), t + 1000);  // 300 ms after the real slap
  tick_for(ref, now, 600 + COUNTER_WINDOW_MS + BLE_GRACE_MS + 100);  // no counter comes
  CHECK(rec.results.size() == 4 && rec.results[3].outcome == OUT_MISS);
  CHECK(rec.results[3].delta_ms == 300);
}

int main() {
  test_judge_table();
  test_strength();
  test_state_machine();
  test_game_over();
  test_feint_penalty_lasts_one_round();
  test_counter();
  test_feint_rules();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("all passed");
  return 0;
}
