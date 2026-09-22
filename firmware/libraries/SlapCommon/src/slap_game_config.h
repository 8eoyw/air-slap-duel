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
// Extra wait after the window closes so late BLE notifications can arrive.
#ifndef BLE_GRACE_MS
#define BLE_GRACE_MS          150
#endif

#ifndef ANNOUNCE_TIMEOUT_MS
#define ANNOUNCE_TIMEOUT_MS   2500  // used when the platform has no TTS-done signal
#endif
#ifndef ATTACK_TIMEOUT_MS
#define ATTACK_TIMEOUT_MS     5000  // re-announce if no attack arrives
#endif
#ifndef SWAP_TIMEOUT_MS
#define SWAP_TIMEOUT_MS       8000  // time for the players to swap devices
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

#define SYNC_PERIOD_MS        1000

// Which dodge escapes which attack. Depends on how the players stand;
// flip if forehand is swung from the attacker's right.
#define DODGE_FOR_FOREHAND    CLS_DODGE_RIGHT
#define DODGE_FOR_BACKHAND    CLS_DODGE_LEFT
