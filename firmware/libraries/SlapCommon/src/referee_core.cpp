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
    case OUT_HIT:          return "HIT";
    case OUT_JUMPED:       return "JUMPED";
    case OUT_GRAZE:        return "GRAZE";
    case OUT_MISS:         return "MISS";
    case OUT_FEINT_FOOLED: return "FEINT_FOOLED";
    case OUT_FEINT_HELD:   return "FEINT_HELD";
  }
  return "?";
}

void RefereeCore::reset() {
  state_ = REF_IDLE;
  entered_ms_ = 0;
  hp_[0] = hp_[1] = HP_START;
  attacker_ = 0;
  round_ = 0;
  feinted_[0] = feinted_[1] = false;
  have_def_ = have_early_def_ = false;
}

void RefereeCore::enter(RefState s, uint32_t now) {
  state_ = s;
  entered_ms_ = now;
  if (out_) out_->onState(s);
  if (s == REF_ANNOUNCE) {
    round_++;
    have_def_ = have_early_def_ = false;
    if (out_) out_->onAnnounce(attacker_, round_);
  }
}

void RefereeCore::start(uint32_t now) {
  if (state_ != REF_IDLE && state_ != REF_GAME_OVER) return;
  reset();
  enter(REF_ANNOUNCE, now);
}

void RefereeCore::announceDone(uint32_t now) {
  if (state_ == REF_ANNOUNCE) enter(REF_ARMED, now);
}

void RefereeCore::swapDone(uint32_t now) {
  if (state_ == REF_SWAP) enter(REF_ANNOUNCE, now);
}

void RefereeCore::onAttackerEvent(const SlapEvent& e, uint32_t now) {
  if (state_ != REF_ARMED || !slapIsAttack(e.cls)) return;
  atk_ = e;
  // A defender event that arrived shortly before the attack onset is a jump.
  if (have_early_def_ && (int32_t)(e.t_onset - early_def_.t_onset) <= JUMP_LOOKBACK_MS &&
      (int32_t)(e.t_onset - early_def_.t_onset) >= 0) {
    have_def_ = true;
    def_ = early_def_;
  }
  enter(REF_JUDGING, now);
}

void RefereeCore::onDefenderEvent(const SlapEvent& e, uint32_t /*now*/) {
  if (!slapIsDefense(e.cls)) return;
  if (state_ == REF_ARMED) {
    have_early_def_ = true;
    early_def_ = e;
  } else if (state_ == REF_JUDGING && !have_def_) {
    // Only the first reaction counts.
    if ((int32_t)(e.t_onset - atk_.t_onset) >= -JUMP_LOOKBACK_MS) {
      have_def_ = true;
      def_ = e;
    }
  }
}

void RefereeCore::tick(uint32_t now) {
  uint32_t dt = now - entered_ms_;
  switch (state_) {
    case REF_ANNOUNCE:
      if (dt >= ANNOUNCE_TIMEOUT_MS) enter(REF_ARMED, now);
      break;
    case REF_ARMED:
      if (dt >= ATTACK_TIMEOUT_MS) {
        round_--;  // same round, re-announced
        enter(REF_ANNOUNCE, now);
      }
      break;
    case REF_JUDGING: {
      // Only the first reaction counts, so resolve as soon as one is in hand.
      // Otherwise wait until the window (plus BLE grace) has passed on the referee clock.
      uint32_t window = feinted_[attacker_ ^ 1] ? FEINTED_WINDOW_MAX_MS : DODGE_WINDOW_MAX_MS;
      if (have_def_ || (int32_t)(now - atk_.t_onset) >= (int32_t)(window + BLE_GRACE_MS)) {
        resolve(now);
      }
      break;
    }
    case REF_RESOLVE:
      if (dt >= RESOLVE_SHOW_MS) {
        if (hp_[0] <= 0 || hp_[1] <= 0) {
          enter(REF_GAME_OVER, now);
          if (out_) out_->onGameOver(hp_[0] > 0 ? 0 : 1, hp_);
        } else {
          attacker_ ^= 1;
          enter(REF_SWAP, now);
          if (out_) out_->onSwap(attacker_);
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

static uint8_t damageFor(RefStrength s) {
  return s == STR_HEAVY ? DMG_HEAVY : s == STR_MEDIUM ? DMG_MEDIUM : DMG_LIGHT;
}

RoundResult RefereeCore::judge(const SlapEvent& atk, const SlapEvent* def, bool feinted_before) {
  RoundResult r = {};
  r.attack_cls = atk.cls;
  r.defense_cls = def ? def->cls : (uint8_t)CLS_BACKGROUND;
  r.strength = strengthOf(atk.peak);
  r.delta_ms = def ? (int32_t)(def->t_onset - atk.t_onset) : 0;
  uint8_t full = damageFor(r.strength);

  if (atk.cls == CLS_FEINT) {
    r.outcome = def ? OUT_FEINT_FOOLED : OUT_FEINT_HELD;
    r.damage = 0;
    return r;
  }

  int32_t max_ms = feinted_before ? FEINTED_WINDOW_MAX_MS : DODGE_WINDOW_MAX_MS;
  if (!def || r.delta_ms > max_ms) {
    r.outcome = OUT_HIT;
    r.damage = full;
  } else if (r.delta_ms < DODGE_WINDOW_MIN_MS) {
    r.outcome = OUT_JUMPED;
    r.damage = full;
  } else {
    uint8_t right = atk.cls == CLS_FOREHAND ? DODGE_FOR_FOREHAND : DODGE_FOR_BACKHAND;
    if (def->cls == CLS_BLOCK || def->cls == right) {
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
  RoundResult r = judge(atk_, have_def_ ? &def_ : nullptr, feinted_[defender]);
  // The shrunk window is a one-round penalty.
  feinted_[defender] = (r.outcome == OUT_FEINT_FOOLED);
  hp_[defender] -= r.damage;
  if (hp_[defender] < 0) hp_[defender] = 0;
  r.attacker = attacker_;
  r.hp[0] = hp_[0];
  r.hp[1] = hp_[1];
  enter(REF_RESOLVE, now);
  if (out_) out_->onResult(r);
}
