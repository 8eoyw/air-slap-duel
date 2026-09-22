// UART mode: firmware/nano_hub runs the referee and prints JSON lines; we turn
// them back into SlapFeedback callbacks and forward commands to the hub.
#include "sdkconfig.h"
#if CONFIG_SLAP_MODE_UART

#include <string.h>

#include "cJSON.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "slap_internal.h"

static const char* TAG = "slap_uart";
static const uart_port_t kPort = (uart_port_t)CONFIG_SLAP_UART_NUM;

namespace {

SlapFeedback* g_fb;

uint8_t cls_from_name(const char* s) {
  static const uint8_t ids[] = {CLS_BACKGROUND, CLS_FOREHAND, CLS_BACKHAND, CLS_FEINT,
                                CLS_DODGE_LEFT, CLS_DODGE_RIGHT, CLS_BLOCK};
  for (uint8_t id : ids)
    if (s && strcmp(s, slapClassName(id)) == 0) return id;
  return CLS_BACKGROUND;
}

int num(const cJSON* o, const char* k, int dflt = 0) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsNumber(v) ? v->valueint : dflt;
}

const char* str(const cJSON* o, const char* k) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsString(v) ? v->valuestring : "";
}

RefState state_from_name(const char* s) {
  for (int i = REF_IDLE; i <= REF_GAME_OVER; i++)
    if (strcmp(s, refStateName((RefState)i)) == 0) return (RefState)i;
  return REF_IDLE;
}

RefOutcome outcome_from_name(const char* s) {
  for (int i = OUT_HIT; i <= OUT_FEINT_HELD; i++)
    if (strcmp(s, refOutcomeName((RefOutcome)i)) == 0) return (RefOutcome)i;
  return OUT_HIT;
}

void read_hp(const cJSON* o, int16_t hp[2]) {
  const cJSON* a = cJSON_GetObjectItemCaseSensitive(o, "hp");
  hp[0] = hp[1] = 0;
  if (cJSON_IsArray(a) && cJSON_GetArraySize(a) == 2) {
    hp[0] = cJSON_GetArrayItem(a, 0)->valueint;
    hp[1] = cJSON_GetArrayItem(a, 1)->valueint;
  }
}

// Runs on the UART task. Feedback callbacks are documented as "from the referee
// task"; in UART mode this task plays that role (the referee task only forwards commands).
void dispatch(const char* line) {
  cJSON* o = cJSON_Parse(line);
  if (!o) return;
  const char* type = str(o, "type");
  if (!strcmp(type, "state")) {
    g_fb->onState(state_from_name(str(o, "state")));
  } else if (!strcmp(type, "announce")) {
    g_fb->onAnnounce(num(o, "attacker"), num(o, "round"));
  } else if (!strcmp(type, "result")) {
    RoundResult r = {};
    r.outcome = outcome_from_name(str(o, "outcome"));
    const char* s = str(o, "strength");
    r.strength = !strcmp(s, "heavy") ? STR_HEAVY : !strcmp(s, "medium") ? STR_MEDIUM : STR_LIGHT;
    r.attack_cls = cls_from_name(str(o, "attack"));
    r.defense_cls = cls_from_name(str(o, "defense"));
    r.delta_ms = num(o, "delta_ms");
    r.damage = num(o, "damage");
    r.attacker = num(o, "attacker");
    read_hp(o, r.hp);
    g_fb->onResult(r);
  } else if (!strcmp(type, "swap")) {
    g_fb->onSwap(num(o, "attacker"));
  } else if (!strcmp(type, "game_over")) {
    int16_t hp[2];
    read_hp(o, hp);
    g_fb->onGameOver(num(o, "winner"), hp);
  } else if (!strcmp(type, "peer")) {
    const cJSON* c = cJSON_GetObjectItemCaseSensitive(o, "connected");
    g_fb->onPeer(str(o, "name"), cJSON_IsTrue(c));
  } else if (!strcmp(type, "event")) {
    SlapEvent e = {};
    e.t_onset = num(o, "t");
    e.cls = cls_from_name(str(o, "cls"));
    e.conf = num(o, "conf");
    e.peak = num(o, "peak");
    g_fb->onEvent(str(o, "from"), e, num(o, "rx"));
  }
  cJSON_Delete(o);
}

void uart_task(void*) {
  static char line[256];
  size_t n = 0;
  uint8_t buf[64];
  for (;;) {
    int got = uart_read_bytes(kPort, buf, sizeof(buf), pdMS_TO_TICKS(20));
    for (int i = 0; i < got; i++) {
      char c = (char)buf[i];
      if (c == '\n') {
        line[n] = 0;
        if (n) dispatch(line);
        n = 0;
      } else if (c != '\r' && n < sizeof(line) - 1) {
        line[n++] = c;
      }
    }
  }
}

}  // namespace

void slap_uart_start(SlapFeedback* fb) {
  g_fb = fb;
  uart_config_t cfg = {};
  cfg.baud_rate = 115200;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_driver_install(kPort, 1024, 0, 0, nullptr, 0));
  ESP_ERROR_CHECK(uart_param_config(kPort, &cfg));
  ESP_ERROR_CHECK(uart_set_pin(kPort, CONFIG_SLAP_UART_TX_GPIO, CONFIG_SLAP_UART_RX_GPIO,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  xTaskCreate(uart_task, "slap_uart", 4096, nullptr, 5, nullptr);
  ESP_LOGI(TAG, "listening for nano_hub on UART%d", (int)kPort);
}

void slap_uart_send_command(SlapCommand cmd) {
  static const char* names[] = {"start\n", "announce_done\n", "swap_done\n", "reset\n"};
  const char* s = names[cmd];
  uart_write_bytes(kPort, s, strlen(s));
}

#endif  // CONFIG_SLAP_MODE_UART
