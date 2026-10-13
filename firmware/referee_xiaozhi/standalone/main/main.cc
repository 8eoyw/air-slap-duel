// Standalone referee: prints the same JSON lines as firmware/nano_hub on the
// console, so tools/dashboard.py works with either. Control:
//   BOOT button (GPIO0) -> start / rematch
//   console lines: start | announce_done | swap_done | reset | left1 | right1 | left2 | right2
//                  status (re-print peers + state; tools/dashboard.py sends it when it opens)
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "slap_referee.h"

namespace {

class JsonFeedback : public SlapFeedback {
 public:
  void onState(RefState s) override {
    state_ = s;
    printf("{\"type\":\"state\",\"state\":\"%s\"}\n", refStateName(s));
  }
  void onAnnounce(uint8_t attacker, uint8_t round) override {
    printf("{\"type\":\"announce\",\"attacker\":%u,\"round\":%u}\n", attacker, round);
  }
  void onResult(const RoundResult& r) override {
    static const char* str[] = {"light", "medium", "heavy"};
    printf("{\"type\":\"result\",\"outcome\":\"%s\",\"strength\":\"%s\",\"attack\":\"%s\","
           "\"defense\":\"%s\",\"delta_ms\":%ld,\"damage\":%u,\"counter_damage\":%u,"
           "\"attacker\":%u,\"hp\":[%d,%d]}\n",
           refOutcomeName(r.outcome), str[r.strength], slapClassName(r.attack_cls),
           slapClassName(r.defense_cls), (long)r.delta_ms, r.damage, r.counter_damage, r.attacker,
           r.hp[0], r.hp[1]);
    if (r.damage || r.counter_damage) slap_servo_hit(r.strength);
  }
  void onSwap(uint8_t next) override { printf("{\"type\":\"swap\",\"attacker\":%u}\n", next); }
  void onGameOver(uint8_t winner, const int16_t hp[2]) override {
    printf("{\"type\":\"game_over\",\"winner\":%u,\"hp\":[%d,%d]}\n", winner, hp[0], hp[1]);
  }
  void onPeer(const char* name, bool connected) override {
    remember(name, connected);
    printPeer(name, connected);
  }
  // A dashboard opened after the boards connected has missed the peer lines.
  void reprint() {
    for (int i = 0; i < npeers_; i++) printPeer(peers_[i].name, peers_[i].connected);
    printf("{\"type\":\"state\",\"state\":\"%s\"}\n", refStateName(state_));
  }
  void onEvent(const char* from, const SlapEvent& e, uint32_t rx) override {
    printf("{\"type\":\"event\",\"from\":\"%s\",\"cls\":\"%s\",\"t\":%lu,\"rx\":%lu,\"conf\":%u,\"peak\":%u}\n",
           from, slapClassName(e.cls), (unsigned long)e.t_onset, (unsigned long)rx, e.conf, e.peak);
  }

 private:
  static void printPeer(const char* name, bool connected) {
    printf("{\"type\":\"peer\",\"name\":\"%s\",\"connected\":%s}\n", name, connected ? "true" : "false");
  }
  // Names are the SLAP_NAME_* string literals, so keeping the pointer is enough.
  void remember(const char* name, bool connected) {
    for (int i = 0; i < npeers_; i++)
      if (!strcmp(peers_[i].name, name)) { peers_[i].connected = connected; return; }
    if (npeers_ < 4) peers_[npeers_++] = {name, connected};
  }
  struct Peer { const char* name; bool connected; };
  Peer peers_[4] = {};
  int npeers_ = 0;
  RefState state_ = REF_IDLE;
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
    else if (!strcmp(line, "left1")) slap_referee_command(SLAP_CMD_LEFT_P1);
    else if (!strcmp(line, "right1")) slap_referee_command(SLAP_CMD_RIGHT_P1);
    else if (!strcmp(line, "left2")) slap_referee_command(SLAP_CMD_LEFT_P2);
    else if (!strcmp(line, "right2")) slap_referee_command(SLAP_CMD_RIGHT_P2);
    else if (!strcmp(line, "status")) g_feedback.reprint();
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
  // NimBLE keeps bonding keys and the PHY its RF calibration in NVS.
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  setvbuf(stdin, nullptr, _IONBF, 0);
  slap_referee_start(&g_feedback);
  // Both tasks print (gpio_config logs too); 2 KB overflowed with the console on UART.
  xTaskCreate(console_task, "console", 4096, nullptr, 3, nullptr);
  xTaskCreate(button_task, "button", 4096, nullptr, 3, nullptr);
}
