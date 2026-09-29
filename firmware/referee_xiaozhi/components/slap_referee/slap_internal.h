// Internal glue between the referee task and the transports.
#pragma once
#include "esp_heap_caps.h"
#include "slap_referee.h"

struct SlapMsg {
  enum Kind : uint8_t { EVENT, PEER, COMMAND } kind;
  uint8_t     peer;       // slot: 0 = SLAP-ATK / SLAP-P1, 1 = SLAP-DEF / SLAP-P2
  bool        connected;  // PEER
  bool        player_board;  // PEER: slot holds a player board (SLAP-P1/P2)
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
