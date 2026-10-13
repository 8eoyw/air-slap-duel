// Referee tuning knobs. See docs/architecture.md for the judging table.
// Override any of these with -D flags or by defining them before including.
#pragma once

#ifndef HP_START
#define HP_START              100
#endif

// Defender reaction window relative to the attack onset (referee clock).
#ifndef DODGE_WINDOW_MIN_MS
#define DODGE_WINDOW_MIN_MS   200   // earlier = jumped the gun, counts as hit
#endif
#ifndef DODGE_WINDOW_MAX_MS
#define DODGE_WINDOW_MAX_MS   600   // later = too slow, counts as hit
#endif
#ifndef FEINTED_WINDOW_MAX_MS
#define FEINTED_WINDOW_MAX_MS 450   // shrunk window after being fooled by a feint
#endif
// Defender events this long before the attack onset still count (as a jump).
#ifndef JUMP_LOOKBACK_MS
#define JUMP_LOOKBACK_MS      300
#endif
// Extra wait after the window closes so late notifications can arrive. A node only
// sends once its window is full, so this must cover the post-trigger part of the
// window ((SLAP_WINDOW - SLAP_PRE) at SAMPLE_HZ = 500 ms) + inference + BLE.
// Measured rx - t_onset on nano_player: 504-527 ms (2026-10-13). 150 ms made every
// defense arrive after judging, so every slap came out HIT.
#ifndef BLE_GRACE_MS
#define BLE_GRACE_MS          600
#endif

#ifndef ANNOUNCE_TIMEOUT_MS
#define ANNOUNCE_TIMEOUT_MS   2500  // used when the platform has no TTS-done signal
#endif
#ifndef ATTACK_TIMEOUT_MS
#define ATTACK_TIMEOUT_MS     5000  // attack phase length; no real slap by then -> turn passes
#endif
#ifndef SWAP_TIMEOUT_MS
#define SWAP_TIMEOUT_MS       8000  // time for the players to swap devices (role boards only)
#endif
#ifndef RESOLVE_SHOW_MS
#define RESOLVE_SHOW_MS       1500  // how long the hit/miss feedback stays up
#endif

// Strength tiers by peak |gyro| (dps). TODO: calibrate from collected data.
#ifndef PEAK_MEDIUM_DPS
#define PEAK_MEDIUM_DPS       400
#endif
#ifndef PEAK_HEAVY_DPS
#define PEAK_HEAVY_DPS        800
#endif
#define DMG_LIGHT             10
#define DMG_MEDIUM            20
#define DMG_HEAVY             30

// Block: always stops a slap, but only halves it and never opens a counter.
// Only a dodge in the right direction avoids all damage and earns a counter.
#define BLOCK_DAMAGE_NUM      1
#define BLOCK_DAMAGE_DEN      2

// Counter: after a clean dodge the defender may slap back.
#ifndef COUNTER_MIN_GAP_MS
#define COUNTER_MIN_GAP_MS    150   // counter onset must come this long after the dodge onset
#endif
#ifndef COUNTER_WINDOW_MS
#define COUNTER_WINDOW_MS     800   // ... and no later than this
#endif
#define COUNTER_MULT_NUM      3     // counter damage = tier damage x 3/2
#define COUNTER_MULT_DEN      2
#define COUNTER_FEEBLE_MULT   2     // x2 instead when the dodged slap was light ("feeble slap opens a counter")

// Feint follow-up: if the defender bites on a feint, a real slap that starts
// after the defender's reaction (and within this window) can't be dodged and crits.
#ifndef FOLLOWUP_WINDOW_MS
#define FOLLOWUP_WINDOW_MS    1000
#endif
#define CRIT_MULT_NUM         3
#define CRIT_MULT_DEN         2

#define SYNC_PERIOD_MS        1000

// Which dodge escapes which attack for a right-handed attacker (a forehand
// swung from the attacker's right). Left-handed attackers are mirrored at
// runtime: RefereeCore::setLeftHanded().
#define DODGE_FOR_FOREHAND    CLS_DODGE_RIGHT
#define DODGE_FOR_BACKHAND    CLS_DODGE_LEFT
