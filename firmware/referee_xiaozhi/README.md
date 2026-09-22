# 裁判端（小智 ESP32-S3）

```
components/slap_referee/   ESP-IDF 组件，不依赖小智固件
standalone/                独立固件：只有裁判，console 输出 JSON
xiaozhi_glue/              接进 xiaozhi-esp32 的胶水（表情、文字、舵机、MCP 语音指令、LLM 嘲讽）
```

裁判核心（判定与状态机）是 `firmware/libraries/SlapCommon/src/referee_core.cpp`，
与 Nano hub、PC 单元测试共用同一份代码。

## 两种模式（`idf.py menuconfig` → Air Slap Duel referee）

| 模式 | 谁连 BLE | 谁跑裁判 | 何时用 |
|---|---|---|---|
| `SLAP_MODE_BLE`（默认） | 小智（NimBLE central） | 小智 | 可行性验证通过时 |
| `SLAP_MODE_UART` | `firmware/nano_hub` | nano_hub | 小智 Wi-Fi + 音频 + BLE 共存不行时（接线：hub D1 → 小智 RX GPIO，GND 共地） |

## 第 1–2 周：可行性验证

1. 两台 Nano 烧 `nano_attacker` / `nano_defender`（没有 model.h 也行，会用启发式分类）。
2. 小智板子烧 standalone：
   ```bash
   cd firmware/referee_xiaozhi/standalone
   idf.py set-target esp32s3
   idf.py build flash monitor
   ```
3. 看 log：
   - [ ] `SLAP-ATK ready`、`SLAP-DEF ready` 都出现
   - [ ] `conn N interval` 是 7.5–15 ms
   - [ ] 每 10 s 的 `free internal heap`
   - [ ] 按 BOOT 键开局，挥手能看到 `event` 和 `result` JSON
4. 再把组件接进 xiaozhi-esp32（下一节），在 Wi-Fi + 语音对话进行中重复第 3 步。
   heap 掉到很低或连线频繁断开 → 改用 UART 模式。

也可以把 standalone 的 USB 接到笔电，用 `python tools/dashboard.py --port COMx` 看记分板。

## 接进 xiaozhi-esp32

1. 把 `components/slap_referee` 和 `firmware/libraries/SlapCommon` 复制进小智的源码树，
   在小智的顶层 `CMakeLists.txt` 设置
   `set(SLAP_COMMON_DIR <SlapCommon/src 的路径>)`，并把 `components/` 加进 `EXTRA_COMPONENT_DIRS`。
2. 把 `xiaozhi_glue/slap_xiaozhi.cc` 放进 `main/`，加进 `main/CMakeLists.txt` 的 SRCS，
   在 `Application::Start()` 显示初始化之后调用 `SlapXiaozhiInit()`。
3. sdkconfig 需要：`CONFIG_BT_ENABLED=y`、`CONFIG_BT_NIMBLE_ENABLED=y`、
   `CONFIG_BT_NIMBLE_ROLE_CENTRAL=y`、`CONFIG_BT_NIMBLE_MAX_CONNECTIONS>=2`（UART 模式不需要）。
4. 语音：对小智说「开始巴掌对决」，LLM 会调用 MCP 工具 `self.slap_duel.start`。
5. 可选：`CONFIG_SLAP_LLM_TAUNT=y`，每次命中后让云端 LLM 说一句嘲讽。只是加料，断网不影响判定。

`xiaozhi_glue` 是依 xiaozhi-esp32 v1.x–v2.x 的 API（`Board::GetDisplay()`、`SetEmotion`、
`McpServer::AddTool`、`Application::Schedule` / `WakeWordInvoke`）写的，**没有对着特定版本编译过**。
如果你们的版本 API 不同，只需要改这一个文件。

## 舵机

`CONFIG_SLAP_SERVO_GPIO` 设成空闲的 GPIO（默认 -1 代表不接）。
使用 LEDC timer 2 / channel 5，避开背光 PWM。舵机用独立 5 V 供电，与板子共地。
