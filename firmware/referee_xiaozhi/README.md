# 裁判端（小智 ESP32-S3）

在小智固件（xiaozhi-esp32，ESP-IDF）上加一个 `slap_referee` 组件。

## 第 1–2 周必做：可行性验证

- [ ] 小智 Wi-Fi + 音频串流开着时，NimBLE central 能否同时连上 `SLAP-ATK` 与 `SLAP-DEF`
- [ ] 记录 free heap / PSRAM 余量
- [ ] 连接间隔能否设到 7.5–15 ms
- 若不可行 → 改用备案：一台 Nano 当 central 汇总，UART JSON 接小智（见 docs/risks.md）

## 组件规划

| 文件 | 内容 |
|---|---|
| `game_config.h` | 判定时间窗、伤害值、HP |
| `ble_central.c` | 扫描、连接、订阅 Event、每 1 s 写 Sync |
| `game_fsm.c` | 回合状态机（见 docs/architecture.md） |
| `feedback.c` | LCD 表情、TTS 旁白、舵机 GPIO / PWM |
| `mcp_tools.c` | 暴露给小智语音的 MCP 工具：`start_game`、`rematch` |

TODO：以上文件尚未创建。
