// Internal glue between the referee task and the transports.
#pragma once
#include "esp_heap_caps.h"
#include "slap_referee.h"

struct SlapMsg {
  enum Kind : uint8_t { EVENT, PEER, COMMAND } kind;
  uint8_t     peer;       // 0 = attacker, 1 = defender
  bool        connected;  // PEER
  SlapCommand cmd;        // COMMAND
  SlapEvent   event;      // EVENT
  uint32_t    rx_ms;      // EVENT: referee clock at reception
};

void slap_post(const SlapMsg& m);

// BLE mode
void slap_ble_start();
void slap_ble_send_sync(uint32_t referee_ms);

// UART mode
void slap_uart_start(SlapFeedback* fb);
void slap_uart_send_command(SlapCommand cmd);
