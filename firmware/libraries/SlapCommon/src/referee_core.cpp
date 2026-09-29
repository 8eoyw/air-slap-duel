#include "referee_core.h"

const char* refStateName(RefState s) {
  switch (s) {
    case REF_IDLE:      return "IDLE";
    case REF_ANNOUNCE:  return "ANNOUNCE";
    case REF_ARMED:     return "ARMED";
    case REF_JUDGING:   return "JUDGING";
    case REF_RESOLVE:   return "RESOLVE";
    case REF_SWAP:      return "SWAP";
    case REF_GAME_OVER: return "GAME_OVER";
  }
  return "?";
}

const char* refOutcomeName(RefOutcome o) {
  switch (o) {
    case OUT_HIT:            return "HIT";
    case OUT_JUMPED:         return "JUMPED";
    case OUT_GRAZE:          return "GRAZE";
    case OUT_MISS:           return "MISS";
    case OUT_FEINT_FOOLED:   return "FEINT_FOOLED";
    case OUT_FEINT_HELD:     return "FEINT_HELD";
    case OUT_BLOCKED:        return "BLOCKED";
    case OUT_COUNTERED:      return "COUNTERED";
    case OUT_FEINT_PUNISHED: return "FEINT_PUNISHED";
    case OUT_TIMEOUT:        return "TIMEOUT";
    case OUT_COUNT_:         break;
  }
  return "?";
}

static bool isSlap(uint8_t c) { return c == CLS_FOREHAND || c == CLS_BACKHAND; }

void RefereeCore::reset() {
  state_ = REF_IDLE;
  entered_ms_ = 0;
  hp_[0] = hp_[1] = HP_START;
  attacker_ = 0;
  round_ = 0;
  feinted_[0] = feinted_[1] = false;
  attack_deadline_ = 0;
  have_def_ = have_early_def_ = have_counter_ = have_follow_ = false;
}

void RefereeCore::enter(RefState s, uint32_t now) {
  state_ = s;
  entered_ms_ = now;
  if (out_) out_->onState(s);
  if (s == REF_ANNOUNCE) {
    round_++;
    if (out_) out_->onAnnounce(attacker_, round_);
  }
}

// Start (or resume, after a held feint) waiting for a slap. Clears the round's
// defense bookkeeping; the attack deadline is set by the caller.
void RefereeCore::arm(uint32_t now) {
  have_def_ = have_early_def_ = have_counter_ = have_follow_ = false;
  enter(REF_ARMED, now);
}

void RefereeCore::start(uint32_t now) {
  if (state_ != REF_IDLE && state_ != REF_GAME_OVER) return;
  reset();
  enter(REF_ANNOUNCE, now);
}

void RefereeCore::announceDone(uint32_t now) {
  if (state_ != REF_ANNOUNCE) return;
  attack_deadline_ = now + ATTACK_TIMEOUT_MS;
  arm(now);
}

void RefereeCore::swapDone(uint32_t now) {
  if (state_ == REF_SWAP) enter(REF_ANNOUNCE, now);
}

void RefereeCore::onPlayerEvent(uint8_t player, const SlapEvent& e, uint32_t now) {
  if ((player & 1) == attacker_) onAttackerEvent(e, now);
  else                           onDefenderEvent(e, now);
}

void RefereeCore::onAttackerEvent(const SlapEvent& e, uint32_t now) {
  if (state_ == REF_ARMED) {
    if (!slapIsAttack(e.cls)) return;
    atk_ = e;
    // A defender event that arrived shortly before the attack onset is a jump.
    int32_t lead = (int32_t)(e.t_onset - early_def_.t_onset);
    if (have_early_def_ && lead >= 0 && lead <= JUMP_LOOKBACK_MS) {
      have_def_ = true;
      def_ = early_def_;
    }
    enter(REF_JUDGING, now);
  } else if (state_ == REF_JUDGING && atk_.cls == CLS_FEINT && isSlap(e.cls) &&
             (int32_t)(e.t_onset - atk_.t_onset) > 0) {
    if (!have_def_) {
      // Real slap before the defender reacted to the feint: judge that slap instead.
      atk_ = e;
    } else if (!have_follow_) {
      have_follow_ = true;
      follow_ = e;
    }
  }
}

void RefereeCore::onDefenderEvent(const SlapEvent& e, uint32_t /*now*/) {
  if (state_ == REF_ARMED) {
    if (!slapIsDefense(e.cls)) return;
    have_early_def_ = true;
    early_def_ = e;
  } else if (state_ == REF_JUDGING) {
    if (!have_def_) {
      // Only the first reaction counts.
      if (slapIsDefense(e.cls) && (int32_t)(e.t_onset - atk_.t_onset) >= -JUMP_LOOKBACK_MS) {
        have_def_ = true;
        def_ = e;
      }
    } else if (!have_counter_ && isSlap(e.cls) && def_.cls != CLS_BLOCK) {
      int32_t gap = (int32_t)(e.t_onset - def_.t_onset);
      if (gap >= COUNTER_MIN_GAP_MS && gap <= COUNTER_WINDOW_MS) {
        have_counter_ = true;
        counter_ = e;
      }
    }
  }
}

uint32_t RefereeCore::defenseWindow() const {
  return feinted_[attacker_ ^ 1] ? FEINTED_WINDOW_MAX_MS : DODGE_WINDOW_MAX_MS;
}

void RefereeCore::tick(uint32_t now) {
  uint32_t dt = now - entered_ms_;
  switch (state_) {
    case REF_ANNOUNCE:
      if (dt >= ANNOUNCE_TIMEOUT_MS) announceDone(now);
      break;
    case REF_ARMED:
      if ((int32_t)(now - attack_deadline_) >= 0) {
        RoundResult r = {};
        r.outcome = OUT_TIMEOUT;
        r.attack_cls = r.defense_cls = CLS_BACKGROUND;
        finish(r, now);
      }
      break;
    case REF_JUDGING: {
      // Events arrive ~0.5 s after their onset, so every wait below is measured
      // on the referee clock from the onset, plus BLE grace.
      int32_t since_atk = (int32_t)(now - atk_.t_onset);
      int32_t window_over = (int32_t)(defenseWindow() + BLE_GRACE_MS);
      if (atk_.cls == CLS_FEINT) {
        if (!have_def_) {
          // Feint held: the attack phase goes on, same deadline.
          if (since_atk >= window_over) arm(now);
        } else if (have_follow_ ||
                   (int32_t)(now - def_.t_onset) >= FOLLOWUP_WINDOW_MS + BLE_GRACE_MS) {
          resolve(now);
        }
        break;
      }
      if (!have_def_) {
        if (since_atk >= window_over) resolve(now);
        break;
      }
      // A clean dodge earns a counter: wait for it before resolving.
      RoundResult pre = judge(atk_, &def_, feinted_[attacker_ ^ 1], left_[attacker_]);
      if (pre.outcome == OUT_MISS && !have_counter_ &&
          (int32_t)(now - def_.t_onset) < COUNTER_WINDOW_MS + BLE_GRACE_MS)
        break;
      resolve(now);
      break;
    }
    case REF_RESOLVE:
      if (dt >= RESOLVE_SHOW_MS) {
        if (hp_[0] <= 0 || hp_[1] <= 0) {
          enter(REF_GAME_OVER, now);
          if (out_) out_->onGameOver(hp_[0] > 0 ? 0 : 1, hp_);
        } else {
          attacker_ ^= 1;
          if (player_boards_) {
            if (out_) out_->onSwap(attacker_);
            enter(REF_ANNOUNCE, now);
          } else {
            enter(REF_SWAP, now);
            if (out_) out_->onSwap(attacker_);
          }
        }
      }
      break;
    case REF_SWAP:
      if (dt >= SWAP_TIMEOUT_MS) enter(REF_ANNOUNCE, now);
      break;
    default:
      break;
  }
}

RefStrength RefereeCore::strengthOf(uint16_t peak) {
  if (peak >= PEAK_HEAVY_DPS) return STR_HEAVY;
  if (peak >= PEAK_MEDIUM_DPS) return STR_MEDIUM;
  return STR_LIGHT;
}

uint8_t RefereeCore::damageFor(RefStrength s) {
  return s == STR_HEAVY ? DMG_HEAVY : s == STR_MEDIUM ? DMG_MEDIUM : DMG_LIGHT;
}

uint8_t RefereeCore::counterDamage(const SlapEvent& dodged, const SlapEvent& counter) {
  uint8_t base = damageFor(strengthOf(counter.peak));
  if (strengthOf(dodged.peak) == STR_LIGHT) return base * COUNTER_FEEBLE_MULT;
  return base * COUNTER_MULT_NUM / COUNTER_MULT_DEN;
}

RoundResult RefereeCore::judge(const SlapEvent& atk, const SlapEvent* def, bool feinted_before,
                               bool attacker_left_handed) {
  RoundResult r = {};
  r.attack_cls = atk.cls;
  r.defense_cls = def ? def->cls : (uint8_t)CLS_BACKGROUND;
  r.strength = strengthOf(atk.peak);
  r.delta_ms = def ? (int32_t)(def->t_onset - atk.t_onset) : 0;
  uint8_t full = damageFor(r.strength);

  int32_t max_ms = feinted_before ? FEINTED_WINDOW_MAX_MS : DODGE_WINDOW_MAX_MS;
  if (!def || r.delta_ms > max_ms) {
    r.outcome = OUT_HIT;
    r.damage = full;
  } else if (r.delta_ms < DODGE_WINDOW_MIN_MS) {
    r.outcome = OUT_JUMPED;
    r.damage = full;
  } else if (def->cls == CLS_BLOCK) {
    r.outcome = OUT_BLOCKED;
    r.damage = full * BLOCK_DAMAGE_NUM / BLOCK_DAMAGE_DEN;
  } else {
    bool fore = atk.cls == CLS_FOREHAND;
    if (attacker_left_handed) fore = !fore;  // a mirrored swing comes from the other side
    uint8_t right = fore ? DODGE_FOR_FOREHAND : DODGE_FOR_BACKHAND;
    if (def->cls == right) {
      r.outcome = OUT_MISS;
      r.damage = 0;
    } else {
      r.outcome = OUT_GRAZE;
      r.damage = full / 2;
    }
  }
  return r;
}

void RefereeCore::resolve(uint32_t now) {
  uint8_t defender = attacker_ ^ 1;
  RoundResult r;
  bool fooled = false;
  if (atk_.cls == CLS_FEINT) {
    // Only reached once the defender bit (have_def_).
    if (have_follow_ && (int32_t)(follow_.t_onset - def_.t_onset) >= 0) {
      r = RoundResult{};
      r.outcome = OUT_FEINT_PUNISHED;
      r.attack_cls = follow_.cls;
      r.defense_cls = def_.cls;
      r.strength = strengthOf(follow_.peak);
      r.delta_ms = (int32_t)(def_.t_onset - follow_.t_onset);
      r.damage = damageFor(r.strength) * CRIT_MULT_NUM / CRIT_MULT_DEN;
    } else if (have_follow_) {
      // Follow-up started before the reaction: an ordinary slap against that reaction.
      r = judge(follow_, &def_, feinted_[defender], left_[attacker_]);
    } else {
      r = RoundResult{};
      r.outcome = OUT_FEINT_FOOLED;
      r.attack_cls = CLS_FEINT;
      r.defense_cls = def_.cls;
      r.delta_ms = (int32_t)(def_.t_onset - atk_.t_onset);
      fooled = true;
    }
  } else {
    r = judge(atk_, have_def_ ? &def_ : nullptr, feinted_[defender], left_[attacker_]);
    if (r.outcome == OUT_MISS && have_counter_) {
      r.outcome = OUT_COUNTERED;
      r.counter_damage = counterDamage(atk_, counter_);
    }
  }
  // The shrunk window is a one-round penalty.
  feinted_[defender] = fooled;
  finish(r, now);
}

void RefereeCore::finish(RoundResult r, uint32_t now) {
  uint8_t defender = attacker_ ^ 1;
  hp_[defender] -= r.damage;
  hp_[attacker_] -= r.counter_damage;
  if (hp_[0] < 0) hp_[0] = 0;
  if (hp_[1] < 0) hp_[1] = 0;
  r.attacker = attacker_;
  r.hp[0] = hp_[0];
  r.hp[1] = hp_[1];
  enter(REF_RESOLVE, now);
  if (out_) out_->onResult(r);
}
