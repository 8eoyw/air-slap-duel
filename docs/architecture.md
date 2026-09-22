# 架构

## 节点职责

| 节点 | 做什么 | 不做什么 |
|---|---|---|
| Nano（攻/守） | 100 Hz 采样 6 轴 IMU；加速度模长超过阈值才开 1 s 窗口推论；送出事件封包 | 不做游戏逻辑、不传 raw 数据 |
| 小智 ESP32-S3 | BLE central 同时连两台 Nano；发 sync beacon；回合状态机；判定；UI、TTS、舵机 | 不做 IMU 推论 |
| 云端 / 笔电（可选） | LLM 依伤害值生成嘲讽台词；dashboard | 不在判定的关键路径上 |

## 回合状态机（裁判端）

```
IDLE ──语音「开始」──→ ANNOUNCE ──旁白结束──→ ARMED
ARMED ──攻击事件(class≠background)──→ JUDGING（开防守时间窗）
ARMED ──逾时 5 s──→ ANNOUNCE（重新宣布）
JUDGING ──防守事件 或 窗口结束──→ RESOLVE
RESOLVE ──扣血、表情、舵机、旁白──→ SWAP（攻守互换）
SWAP ──任一方 HP ≤ 0──→ GAME_OVER ──语音「再来一局」──→ IDLE
SWAP ──否则──→ ANNOUNCE
```

## 判定规则（初版，可调）

令 `Δ = t_defend − t_attack`（皆为对时后的时间）。

| 条件 | 结果 | 伤害 |
|---|---|---|
| 攻击为 `feint`，防守有动作 | 防守方被骗，下回合防守窗口缩短 | 0 |
| 无防守事件，或 `Δ > 600 ms` | 命中 HIT | 轻 10 / 中 20 / 重 30 |
| `200 ms ≤ Δ ≤ 600 ms` 且方向正确（正手→右闪、反手→左闪）或 `block` | 闪开 MISS | 0 |
| `Δ < 200 ms` | 抢跑，视为命中 | 同 HIT |
| 方向错误 | 擦过 GRAZE | HIT 的一半 |

参数集中放在 `firmware/referee_xiaozhi/game_config.h`。

## 数据流与带宽

每个事件 8 bytes（见 [protocol.md](protocol.md)）。对照组「串 raw IMU」是 6 轴 × 2 bytes × 100 Hz = 1.2 KB/s/节点，
这组对比是 [experiments.md](experiments.md) 的实验 3。
