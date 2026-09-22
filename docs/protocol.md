# BLE 协定

Nano 是 peripheral，裁判（小智或 nano_hub）是 central。定义在
[`slap_protocol.h`](../firmware/libraries/SlapCommon/src/slap_protocol.h)，所有端共用。

## GATT

| 项目 | UUID | 属性 | 说明 |
|---|---|---|---|
| Service | `5a1a0000-8c3b-4f7e-9d2a-000000000001` | — | Air Slap 服务 |
| Event | `5a1a0001-…-000000000001` | Notify | 事件封包 |
| Sync | `5a1a0002-…-000000000001` | Write / Write w/o response | 裁判时间 beacon |
| Role | `5a1a0003-…-000000000001` | Read | 0 = 攻击者，1 = 防守者 |
| Raw | `5a1a0004-…-000000000001` | Notify | 只在 `-DSLAP_STREAM_RAW` 编译时发送（实验 3） |

广播名称：`SLAP-ATK` / `SLAP-DEF`。ArduinoBLE 把名称放在 scan response 里，central 要用主动扫描。
连线间隔请求 7.5–15 ms。

## 事件封包（8 bytes，little-endian）

| offset | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | `uint32` | `t_onset` | 触发时刻，已换算到裁判时钟（ms） |
| 4 | `uint8` | `cls` | 类别编号，见 `SlapClass` |
| 5 | `uint8` | `conf` | 信心度 × 255 |
| 6 | `uint16` | `peak` | 窗口内 gyro 模长峰值（dps），用来分力道 |

`t_onset` 是**触发点**（越过阈值那一刻），不是封包送出时间。窗口还要再收
`SLAP_WINDOW - SLAP_PRE` 个样本才推论，所以到达裁判时已经晚约 0.5 s + 推论 + BLE；
裁判判定只看时间戳差，不受这段延迟影响。

## Raw 封包（14 bytes，实验 3）

`uint16 seq` + `int16 a[3]`（mg）+ `int16 g[3]`（0.1 dps），每个样本一包。

## 对时

1. 裁判每 1 s 写一次 Sync：`uint32 referee_ms`。
2. Nano 记录 `offset = referee_ms − millis()`，取最近 8 次的中位数，滤掉 BLE jitter。
3. 送事件时填 `t_onset = local_onset_ms + offset`。重新连线时清空历史。

## 节点 serial log（`tools/analyze_log.py` 解析）

```
INFO,<key>=<value>                       启动信息、arena_used、连线/断线
TRIG,<t_local_ms>                        越过阈值
EVT,<t_local>,<t_ref>,<cls>,<conf>,<peak>,<infer_us>   已送出的事件
REJ,<t_local>,<cls>,<conf>,<infer_us>    推论为 background 或信心度不足，未送出
SYNC,<offset>,<median_offset>
```

## 裁判 JSON 输出（nano_hub 与 standalone 相同，`tools/dashboard.py` 解析）

```json
{"type":"announce","attacker":0,"round":1}
{"type":"event","from":"SLAP-ATK","cls":"forehand","t":1000,"rx":1540,"conf":240,"peak":850}
{"type":"result","outcome":"HIT","strength":"heavy","attack":"forehand","defense":"background","delta_ms":0,"damage":30,"attacker":0,"hp":[100,70]}
{"type":"swap","attacker":1}
{"type":"game_over","winner":0,"hp":[40,0]}
{"type":"peer","name":"SLAP-DEF","connected":true}
{"type":"state","state":"ARMED"}
```

指令（一行一个）：`start`、`announce_done`、`swap_done`、`reset`。
