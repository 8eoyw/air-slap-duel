// Simulated match on the PC: two scripted players feed the real referee core
// (firmware/libraries/SlapCommon/src/referee_core.cpp) and the program prints the
// same JSON lines as firmware/referee_xiaozhi/standalone. Lets tools/dashboard.py
// and the report be worked on without any hardware.
//
//   make -C tests sim
//   ../build/sim_match --seed 7 > results/sim.jsonl                 # instant
//   ../build/sim_match --realtime | python tools/dashboard.py --stdin   # live
//
// Event latency (rx - t_onset) defaults to 515 ms, measured on a Nano 33 BLE Sense
// running nano_player (1 s window, trigger in the middle) on 2026-10-13.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include "referee_core.h"

namespace {

const char* kNames[2] = {SLAP_NAME_P1, SLAP_NAME_P2};

struct Pending {
  uint32_t rx;
  uint8_t player;
  SlapEvent e;
};

std::mt19937 rng;
uint32_t g_now = 0;
std::vector<Pending> g_queue;
int g_latency = 515;
bool g_announced = false;
uint32_t g_announce_at = 0;

int uni(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
bool chance(int pct) { return uni(0, 99) < pct; }

void push(uint8_t player, uint32_t t, uint8_t cls, uint16_t peak) {
  SlapEvent e{};
  e.t_onset = t;
  e.cls = cls;
  e.conf = (uint8_t)uni(185, 255);
  e.peak = peak;
  g_queue.push_back({t + (uint32_t)(g_latency + uni(0, 20)), player, e});
}

uint8_t correctDodge(uint8_t slap) { return slap == CLS_FOREHAND ? DODGE_FOR_FOREHAND : DODGE_FOR_BACKHAND; }
uint8_t wrongDodge(uint8_t slap) { return slap == CLS_FOREHAND ? DODGE_FOR_BACKHAND : DODGE_FOR_FOREHAND; }

// One real slap at `t` from `atk`, plus whatever the defender does about it.
void planSlap(uint8_t atk, uint32_t t) {
  uint8_t def = atk ^ 1;
  uint8_t slap = chance(50) ? CLS_FOREHAND : CLS_BACKHAND;
  push(atk, t, slap, (uint16_t)uni(150, 1000));
  int roll = uni(0, 99);
  if (roll < 15) return;                                                   // froze -> HIT
  if (roll < 25) { push(def, t + uni(-150, 150), correctDodge(slap), 300); return; }  // jumped
  if (roll < 45) { push(def, t + uni(250, 550), CLS_BLOCK, 250); return; }
  uint32_t d = t + uni(220, 680);                                          // dodge (some too late)
  bool right = chance(70);
  push(def, d, right ? correctDodge(slap) : wrongDodge(slap), 300);
  if (right && chance(50))
    push(def, d + uni(200, 650), chance(50) ? CLS_FOREHAND : CLS_BACKHAND, (uint16_t)uni(300, 1000));
}

// Called when a round is announced; schedules the whole attack phase.
void planRound(uint8_t atk, uint32_t armed) {
  uint8_t def = atk ^ 1;
  if (chance(8)) return;  // attacker never commits -> TIMEOUT
  uint32_t t = armed + uni(600, 2500);
  if (chance(20)) {       // feint first
    push(atk, t, CLS_FEINT, 300);
    if (chance(55)) {     // defender bites
      uint32_t d = t + uni(250, 450);
      push(def, d, chance(50) ? CLS_DODGE_LEFT : CLS_DODGE_RIGHT, 300);
      if (chance(65)) push(atk, d + uni(250, 800), chance(50) ? CLS_FOREHAND : CLS_BACKHAND,
                           (uint16_t)uni(400, 1000));
      return;
    }
    t += uni(900, 1600);
  }
  planSlap(atk, t);
}

class Json : public RefereeOutput {
 public:
  void onState(RefState s) override {
    std::printf("{\"type\":\"state\",\"state\":\"%s\"}\n", refStateName(s));
  }
  void onAnnounce(uint8_t attacker, uint8_t round) override {
    std::printf("{\"type\":\"announce\",\"attacker\":%u,\"round\":%u}\n", attacker, round);
    g_announced = true;
    g_announce_at = g_now + 1200;  // pretend TTS takes 1.2 s
    planRound(attacker, g_announce_at);
  }
  void onResult(const RoundResult& r) override {
    static const char* str[] = {"light", "medium", "heavy"};
    std::printf("{\"type\":\"result\",\"outcome\":\"%s\",\"strength\":\"%s\",\"attack\":\"%s\","
                "\"defense\":\"%s\",\"delta_ms\":%ld,\"damage\":%u,\"counter_damage\":%u,"
                "\"attacker\":%u,\"hp\":[%d,%d]}\n",
                refOutcomeName(r.outcome), str[r.strength], slapClassName(r.attack_cls),
                slapClassName(r.defense_cls), (long)r.delta_ms, r.damage, r.counter_damage,
                r.attacker, r.hp[0], r.hp[1]);
    // Anything the players were still going to do belongs to the round that just ended.
    g_queue.clear();
  }
  void onSwap(uint8_t next) override { std::printf("{\"type\":\"swap\",\"attacker\":%u}\n", next); }
  void onGameOver(uint8_t winner, const int16_t hp[2]) override {
    std::printf("{\"type\":\"game_over\",\"winner\":%u,\"hp\":[%d,%d]}\n", winner, hp[0], hp[1]);
  }
};

}  // namespace

int main(int argc, char** argv) {
  unsigned seed = (unsigned)std::random_device{}();
  bool realtime = false;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = (unsigned)std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--realtime")) realtime = true;
    else if (!std::strcmp(argv[i], "--latency") && i + 1 < argc) g_latency = std::atoi(argv[++i]);
    else {
      std::fprintf(stderr, "usage: sim_match [--seed N] [--realtime] [--latency MS]\n");
      return 2;
    }
  }
  rng.seed(seed);
  // Unbuffered: the Windows CRT treats _IOLBF as full buffering, which stalls --realtime pipes.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  Json out;
  RefereeCore ref(&out);
  ref.setPlayerBoards(true);
  for (const char* n : kNames) std::printf("{\"type\":\"peer\",\"name\":\"%s\",\"connected\":true}\n", n);
  ref.start(g_now);

  const uint32_t kStep = 10, kLimit = 30 * 60 * 1000;  // give up after 30 simulated minutes
  auto wall = std::chrono::steady_clock::now();
  while (ref.state() != REF_GAME_OVER && g_now < kLimit) {
    if (g_announced && g_now >= g_announce_at) {
      g_announced = false;
      ref.announceDone(g_now);
    }
    for (size_t i = 0; i < g_queue.size();) {
      if (g_queue[i].rx > g_now) { i++; continue; }
      Pending p = g_queue[i];
      g_queue.erase(g_queue.begin() + (long)i);
      std::printf("{\"type\":\"event\",\"from\":\"%s\",\"cls\":\"%s\",\"t\":%lu,\"rx\":%lu,\"conf\":%u,\"peak\":%u}\n",
                  kNames[p.player], slapClassName(p.e.cls), (unsigned long)p.e.t_onset,
                  (unsigned long)g_now, p.e.conf, p.e.peak);
      ref.onPlayerEvent(p.player, p.e, g_now);
      i = 0;  // the callback may have cleared or changed the queue
    }
    ref.tick(g_now);
    if (std::ferror(stdout)) return 0;  // reader went away (dashboard closed)
    g_now += kStep;
    if (realtime) {
      wall += std::chrono::milliseconds(kStep);
      std::this_thread::sleep_until(wall);
    }
  }
  if (ref.state() != REF_GAME_OVER) {
    std::fprintf(stderr, "no winner after %u ms (seed %u)\n", (unsigned)kLimit, seed);
    return 1;
  }
  std::fprintf(stderr, "seed %u, %u rounds, %.1f s simulated\n", seed, ref.round(), g_now / 1000.0);
  return 0;
}
