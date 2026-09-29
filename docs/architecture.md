# 架构

## 节点职责

| 节点 | 做什么 | 不做什么 |
|---|---|---|
| Nano（攻/守） | 100 Hz 采样 6 轴 IMU；加速度模长超过阈值才开 1 s 窗口推论；送出事件封包 | 不做游戏逻辑、不传 raw 数据 |
| 小智 ESP32-S3 | BLE central 同时连两台 Nano；发 sync beacon；回合状态机；判定；UI、TTS、舵机 | 不做 IMU 推论 |
| 云端 / 笔电（可选） | LLM 依伤害值生成嘲讽台词；dashboard | 不在判定的关键路径上 |

## 回合状态机（裁判端）

```
IDLE ──语音「开始」──→ ANNOUNCE ──旁白结束──→ ARMED（攻击阶段 5 s 开始计时）
ARMED ──攻击事件 forehand/backhand/feint──→ JUDGING（开防守时间窗）
ARMED ──攻击阶段逾时──→ RESOLVE（TIMEOUT，0 伤害）
JUDGING ──feint 且防守方没动──→ ARMED（时限照算）
JUDGING ──闪对方向──→ 等反击窗口（闪避起点 +150–800 ms）──→ RESOLVE
JUDGING ──防守方咬了 feint──→ 等追击窗口（防守起点 +1 s）──→ RESOLVE
JUDGING ──其他防守事件 或 窗口结束──→ RESOLVE
RESOLVE ──扣血（含反击伤害）、表情、舵机、旁白──→ 换攻守
  玩家板（SLAP-P1/P2）：直接 ANNOUNCE
  单角色板（SLAP-ATK/DEF）：SWAP 等玩家交换装备 ──→ ANNOUNCE
任一方 HP ≤ 0 ──→ GAME_OVER ──语音「再来一局」──→ IDLE
```

## 判定规则

完整规则表见 [README 的「游戏规则」](../README.md#游戏规则)，参数集中在
`firmware/libraries/SlapCommon/src/slap_game_config.h`（可用 -D 覆盖）。

## 事件路由

- 玩家板：BLE 槽位 = 玩家编号。事件来自当前攻击方 → 走攻击路径（只收巴掌 / 假动作 / 追击）；
  来自防守方 → 走防守路径（闪避、格挡，闪开后再收一记巴掌当反击）。
- 单角色板：槽位 0 永远是攻击装备、槽位 1 是防守装备，玩家每回合交换。

## 数据流与带宽

每个事件 8 bytes（见 [protocol.md](protocol.md)）。对照组「串 raw IMU」是 6 轴 × 2 bytes × 100 Hz = 1.2 KB/s/节点，
这组对比是 [experiments.md](experiments.md) 的实验 3。
