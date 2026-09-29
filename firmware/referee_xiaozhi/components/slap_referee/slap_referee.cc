#include "slap_referee.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "slap_internal.h"

static const char* TAG = "slap_ref";

uint32_t slap_now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }

namespace {

QueueHandle_t g_queue;
SlapFeedback* g_fb;
bool g_player_board[2];  // per slot, from the last PEER message

const char* slot_name(uint8_t slot) {
  if (g_player_board[slot & 1]) return slot ? SLAP_NAME_P2 : SLAP_NAME_P1;
  return slot ? SLAP_NAME_DEFENDER : SLAP_NAME_ATTACKER;
}

#if CONFIG_SLAP_MODE_BLE
RefereeCore* g_core;
#endif

void referee_task(void*) {
  SlapMsg m;
  uint32_t last_sync = 0, last_heap = 0;
  for (;;) {
    // 10 ms tick keeps the judging resolution well under the 200 ms window edges.
    if (xQueueReceive(g_queue, &m, pdMS_TO_TICKS(10)) == pdTRUE) {
      [[maybe_unused]] uint32_t now = slap_now_ms();
      switch (m.kind) {
        case SlapMsg::EVENT:
          g_fb->onEvent(slot_name(m.peer), m.event, m.rx_ms);
#if CONFIG_SLAP_MODE_BLE
          if (g_player_board[m.peer & 1]) g_core->onPlayerEvent(m.peer, m.event, now);
          else if (m.peer == 0)           g_core->onAttackerEvent(m.event, now);
          else                            g_core->onDefenderEvent(m.event, now);
#endif
          break;
        case SlapMsg::PEER:
          g_player_board[m.peer & 1] = m.player_board;
#if CONFIG_SLAP_MODE_BLE
          if (m.connected) g_core->setPlayerBoards(m.player_board);
#endif
          g_fb->onPeer(slot_name(m.peer), m.connected);
          break;
        case SlapMsg::COMMAND:
#if CONFIG_SLAP_MODE_BLE
          switch (m.cmd) {
            case SLAP_CMD_START:         g_core->start(now); break;
            case SLAP_CMD_ANNOUNCE_DONE: g_core->announceDone(now); break;
            case SLAP_CMD_SWAP_DONE:     g_core->swapDone(now); break;
            case SLAP_CMD_RESET:         g_core->reset(); break;
            case SLAP_CMD_LEFT_P1:       g_core->setLeftHanded(0, true); break;
            case SLAP_CMD_RIGHT_P1:      g_core->setLeftHanded(0, false); break;
            case SLAP_CMD_LEFT_P2:       g_core->setLeftHanded(1, true); break;
            case SLAP_CMD_RIGHT_P2:      g_core->setLeftHanded(1, false); break;
          }
#else
          slap_uart_send_command(m.cmd);
#endif
          break;
      }
    }
    uint32_t now = slap_now_ms();
#if CONFIG_SLAP_MODE_BLE
    g_core->tick(now);
    if (now - last_sync >= SYNC_PERIOD_MS) {
      last_sync = now;
      slap_ble_send_sync(now);
    }
#else
    (void)last_sync;
#endif
    if (now - last_heap >= 10000) {  // feasibility check: watch internal RAM headroom
      last_heap = now;
      ESP_LOGI(TAG, "free internal heap %u, min ever %u",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    }
  }
}

}  // namespace

void slap_post(const SlapMsg& m) {
  if (g_queue) xQueueSend(g_queue, &m, 0);
}

void slap_referee_command(SlapCommand cmd) {
  SlapMsg m = {};
  m.kind = SlapMsg::COMMAND;
  m.cmd = cmd;
  slap_post(m);
}

void slap_referee_start(SlapFeedback* feedback) {
  if (g_queue) return;
  g_fb = feedback;
  g_queue = xQueueCreate(16, sizeof(SlapMsg));
#if CONFIG_SLAP_MODE_BLE
  g_core = new RefereeCore(feedback);
#endif
  slap_servo_init();
  xTaskCreate(referee_task, "slap_ref", 4096, nullptr, 5, nullptr);
#if CONFIG_SLAP_MODE_BLE
  slap_ble_start();
  ESP_LOGI(TAG, "started in BLE central mode");
#else
  slap_uart_start(feedback);
  ESP_LOGI(TAG, "started in UART (nano_hub) mode");
#endif
}
