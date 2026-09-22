// Air Slap Duel referee component for ESP-IDF (XiaoZhi ESP32-S3 or any S3 board).
//
// Mode (Kconfig SLAP_MODE):
//   BLE  — this chip is the BLE central for both wearables and runs RefereeCore.
//   UART — firmware/nano_hub runs the referee; this chip only renders its JSON lines.
// Either way the platform sees the same SlapFeedback callbacks.
#pragma once
#include <stdint.h>
#include "referee_core.h"

enum SlapCommand : uint8_t {
  SLAP_CMD_START,          // voice "开始" / button
  SLAP_CMD_ANNOUNCE_DONE,  // TTS finished (optional, otherwise timeout)
  SLAP_CMD_SWAP_DONE,      // players swapped devices (optional, otherwise timeout)
  SLAP_CMD_RESET,
};

// Implement this on the platform (LCD face, sound, servo, LLM taunt ...).
// Called from the referee task, never from an ISR; keep work short or hand it off.
class SlapFeedback : public RefereeOutput {
 public:
  virtual void onPeer(const char* /*name*/, bool /*connected*/) {}
  // Every classified event from a wearable (for logs / dashboard).
  virtual void onEvent(const char* /*from*/, const SlapEvent& /*e*/, uint32_t /*rx_ms*/) {}
};

// Starts the referee task (and BLE host or UART reader). Call once.
void slap_referee_start(SlapFeedback* feedback);
// Thread-safe; may be called from MCP tools, buttons, other tasks.
void slap_referee_command(SlapCommand cmd);

// Referee clock in ms (the clock the wearables are synced to in BLE mode).
uint32_t slap_now_ms();

// Optional servo on CONFIG_SLAP_SERVO_GPIO. No-ops when the GPIO is -1.
void slap_servo_init();
void slap_servo_hit(uint8_t strength);  // RefStrength: swing further for heavier hits
