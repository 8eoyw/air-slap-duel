// Referee tuning knobs. See docs/architecture.md for the judging table.
#pragma once

#define HP_START            100

#define DODGE_WINDOW_MIN_MS 200   // earlier than this = jumped the gun, counts as hit
#define DODGE_WINDOW_MAX_MS 600   // later than this = too slow, counts as hit
#define ATTACK_TIMEOUT_MS   5000  // re-announce if no attack arrives

// Strength tiers by peak |gyro| (dps). TODO: calibrate from collected data.
#define PEAK_MEDIUM_DPS     400
#define PEAK_HEAVY_DPS      800
#define DMG_LIGHT           10
#define DMG_MEDIUM          20
#define DMG_HEAVY           30

#define SYNC_PERIOD_MS      1000
