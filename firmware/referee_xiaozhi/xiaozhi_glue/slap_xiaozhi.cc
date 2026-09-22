// Glue between the slap_referee component and the xiaozhi-esp32 firmware.
// Copy into xiaozhi-esp32/main/ and add to main/CMakeLists.txt SRCS; call
// SlapXiaozhiInit() once from Application::Start() after the display is up.
//
// UNVERIFIED against a specific xiaozhi-esp32 release: the Board / Display /
// Application / McpServer calls below follow the v1.x-v2.x API. If your tree
// differs, only this file needs adjusting — the component is independent of it.
#include "slap_referee.h"

#include <string>

#include "application.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"

namespace {

const char* kOutcomeZh[] = {"命中！", "抢跑，算你挨打", "擦过", "闪开了", "被假动作骗了", "定力不错"};
const char* kStrengthZh[] = {"轻", "中", "重"};

class XiaozhiFeedback : public SlapFeedback {
 public:
  void onAnnounce(uint8_t attacker, uint8_t round) override {
    Say("neutral", "第" + std::to_string(round) + "回合，玩家" + std::to_string(attacker + 1) + "出招！");
  }

  void onResult(const RoundResult& r) override {
    std::string msg = std::string(kOutcomeZh[r.outcome]);
    if (r.damage) msg += " " + std::string(kStrengthZh[r.strength]) + "击 -" + std::to_string(r.damage);
    msg += "  HP " + std::to_string(r.hp[0]) + " : " + std::to_string(r.hp[1]);
    const char* face = r.damage >= DMG_HEAVY ? "crying" : r.damage ? "sad" : r.outcome == OUT_MISS ? "cool" : "surprised";
    Say(face, msg);
    if (r.damage) slap_servo_hit(r.strength);

#if CONFIG_SLAP_LLM_TAUNT
    // Online bonus: let the cloud LLM improvise one line of trash talk.
    // Hard real-time judging has already happened locally; this is garnish.
    if (r.damage) {
      std::string prompt = "（巴掌对决裁判旁白）玩家" + std::to_string(r.attacker + 1) + "用" +
                           kStrengthZh[r.strength] + "力打中了对手，对手剩" +
                           std::to_string(r.hp[r.attacker ^ 1]) + "血。用一句话幽默地嘲讽被打的一方，不超过20字。";
      Application::GetInstance().Schedule([prompt]() { Application::GetInstance().WakeWordInvoke(prompt); });
    }
#endif
  }

  void onSwap(uint8_t next) override {
    Say("neutral", "攻守交换，请交换装备。玩家" + std::to_string(next + 1) + "准备出招");
  }

  void onGameOver(uint8_t winner, const int16_t hp[2]) override {
    Say("laughing", "玩家" + std::to_string(winner + 1) + "获胜！" + std::to_string(hp[0]) + " : " +
                        std::to_string(hp[1]) + "  说“再来一局”重新开始");
  }

  void onPeer(const char* name, bool connected) override {
    Say(connected ? "happy" : "confused", std::string(name) + (connected ? " 已连接" : " 断线了"));
  }

 private:
  // Display calls must run on the main task.
  static void Say(const char* emotion, std::string text) {
    Application::GetInstance().Schedule([emotion, text]() {
      auto display = Board::GetInstance().GetDisplay();
      display->SetEmotion(emotion);
      display->SetChatMessage("system", text.c_str());
    });
  }
};

XiaozhiFeedback g_feedback;

}  // namespace

void SlapXiaozhiInit() {
  slap_referee_start(&g_feedback);

  // Voice control: the LLM calls these when the user says "开始巴掌对决" / "再来一局".
  auto& mcp = McpServer::GetInstance();
  mcp.AddTool("self.slap_duel.start", "开始或重新开始一局空气巴掌对决游戏", PropertyList(),
              [](const PropertyList&) -> ReturnValue {
                slap_referee_command(SLAP_CMD_START);
                return true;
              });
  mcp.AddTool("self.slap_duel.swap_done", "玩家已经交换好装备，继续下一回合", PropertyList(),
              [](const PropertyList&) -> ReturnValue {
                slap_referee_command(SLAP_CMD_SWAP_DONE);
                return true;
              });
  mcp.AddTool("self.slap_duel.stop", "结束巴掌对决游戏", PropertyList(),
              [](const PropertyList&) -> ReturnValue {
                slap_referee_command(SLAP_CMD_RESET);
                return true;
              });
}
