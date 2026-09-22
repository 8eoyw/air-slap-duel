// Standalone referee: prints the same JSON lines as firmware/nano_hub on the
// console, so tools/dashboard.py works with either. Control:
//   BOOT button (GPIO0) -> start / rematch
//   console lines: start | announce_done | swap_done | reset
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "slap_referee.h"

namespace {

class JsonFeedback : public SlapFeedback {
 public:
  void onState(RefState s) override {
    printf("{\"type\":\"state\",\"state\":\"%s\"}\n", refStateName(s));
  }
  void onAnnounce(uint8_t attacker, uint8_t round) override {
    printf("{\"type\":\"announce\",\"attacker\":%u,\"round\":%u}\n", attacker, round);
  }
  void onResult(const RoundResult& r) override {
    static const char* str[] = {"light", "medium", "heavy"};
    printf("{\"type\":\"result\",\"outcome\":\"%s\",\"strength\":\"%s\",\"attack\":\"%s\","
           "\"defense\":\"%s\",\"delta_ms\":%ld,\"damage\":%u,\"attacker\":%u,\"hp\":[%d,%d]}\n",
           refOutcomeName(r.outcome), str[r.strength], slapClassName(r.attack_cls),
           slapClassName(r.defense_cls), (long)r.delta_ms, r.damage, r.attacker, r.hp[0], r.hp[1]);
    if (r.damage) slap_servo_hit(r.strength);
  }
  void onSwap(uint8_t next) override { printf("{\"type\":\"swap\",\"attacker\":%u}\n", next); }
  void onGameOver(uint8_t winner, const int16_t hp[2]) override {
    printf("{\"type\":\"game_over\",\"winner\":%u,\"hp\":[%d,%d]}\n", winner, hp[0], hp[1]);
  }
  void onPeer(const char* name, bool connected) override {
    printf("{\"type\":\"peer\",\"name\":\"%s\",\"connected\":%s}\n", name, connected ? "true" : "false");
  }
  void onEvent(const char* from, const SlapEvent& e, uint32_t rx) override {
    printf("{\"type\":\"event\",\"from\":\"%s\",\"cls\":\"%s\",\"t\":%lu,\"rx\":%lu,\"conf\":%u,\"peak\":%u}\n",
           from, slapClassName(e.cls), (unsigned long)e.t_onset, (unsigned long)rx, e.conf, e.peak);
  }
};

JsonFeedback g_feedback;

void console_task(void*) {
  char line[32];
  for (;;) {
    if (!fgets(line, sizeof(line), stdin)) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    line[strcspn(line, "\r\n")] = 0;
    if (!strcmp(line, "start")) slap_referee_command(SLAP_CMD_START);
    else if (!strcmp(line, "announce_done")) slap_referee_command(SLAP_CMD_ANNOUNCE_DONE);
    else if (!strcmp(line, "swap_done")) slap_referee_command(SLAP_CMD_SWAP_DONE);
    else if (!strcmp(line, "reset")) slap_referee_command(SLAP_CMD_RESET);
  }
}

void button_task(void*) {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << GPIO_NUM_0;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io);
  int last = 1;
  for (;;) {
    int v = gpio_get_level(GPIO_NUM_0);
    if (last == 1 && v == 0) slap_referee_command(SLAP_CMD_START);
    last = v;
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

}  // namespace

extern "C" void app_main() {
  setvbuf(stdin, nullptr, _IONBF, 0);
  slap_referee_start(&g_feedback);
  xTaskCreate(console_task, "console", 3072, nullptr, 3, nullptr);
  xTaskCreate(button_task, "button", 2048, nullptr, 3, nullptr);
}
