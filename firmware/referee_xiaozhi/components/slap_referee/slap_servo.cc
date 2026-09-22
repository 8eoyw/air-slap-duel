// Hobby servo on LEDC: 50 Hz, 0.5–2.5 ms pulse. A hit swings the prop and
// returns it after a moment. Uses its own LEDC timer/channel (Kconfig) so it
// does not fight the LCD backlight PWM.
#include "sdkconfig.h"
#include "slap_referee.h"

#if CONFIG_SLAP_SERVO_GPIO >= 0
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = (ledc_timer_t)CONFIG_SLAP_SERVO_LEDC_TIMER;
constexpr ledc_channel_t kChan = (ledc_channel_t)CONFIG_SLAP_SERVO_LEDC_CHANNEL;
constexpr uint32_t kBits = 14;
esp_timer_handle_t g_return_timer;

void set_angle(int deg) {
  if (deg < 0) deg = 0;
  if (deg > 180) deg = 180;
  uint32_t us = 500 + (uint32_t)deg * 2000 / 180;
  uint32_t duty = us * ((1u << kBits) - 1) / 20000;  // 20 ms period
  ledc_set_duty(kMode, kChan, duty);
  ledc_update_duty(kMode, kChan);
}

void on_return(void*) { set_angle(CONFIG_SLAP_SERVO_REST_DEG); }
}  // namespace

void slap_servo_init() {
  ledc_timer_config_t t = {};
  t.speed_mode = kMode;
  t.duty_resolution = (ledc_timer_bit_t)kBits;
  t.timer_num = kTimer;
  t.freq_hz = 50;
  t.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&t));
  ledc_channel_config_t c = {};
  c.gpio_num = CONFIG_SLAP_SERVO_GPIO;
  c.speed_mode = kMode;
  c.channel = kChan;
  c.timer_sel = kTimer;
  c.duty = 0;
  c.hpoint = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&c));
  esp_timer_create_args_t a = {};
  a.callback = on_return;
  a.name = "servo_ret";
  ESP_ERROR_CHECK(esp_timer_create(&a, &g_return_timer));
  set_angle(CONFIG_SLAP_SERVO_REST_DEG);
}

void slap_servo_hit(uint8_t strength) {
  static const int swing[] = {25, 45, 70};  // light / medium / heavy
  set_angle(CONFIG_SLAP_SERVO_REST_DEG + swing[strength > 2 ? 2 : strength]);
  esp_timer_stop(g_return_timer);
  esp_timer_start_once(g_return_timer, 350 * 1000);
}

#else
void slap_servo_init() {}
void slap_servo_hit(uint8_t) {}
#endif
