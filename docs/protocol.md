# BLE 协定

Nano 是 peripheral，小智是 central。定义写在 [`firmware/common/slap_protocol.h`](../firmware/common/slap_protocol.h)，两端共用。

## GATT

| 项目 | UUID | 属性 | 说明 |
|---|---|---|---|
| Service | `5a1a0000-8c3b-4f7e-9d2a-000000000001` | — | Air Slap 服务 |
| Event | `5a1a0001-…-000000000001` | Notify | 事件封包 |
| Sync | `5a1a0002-…-000000000001` | Write w/o response | 裁判时间 beacon |
| Role | `5a1a0003-…-000000000001` | Read | 0 = 攻击者，1 = 防守者 |

广播名称：`SLAP-ATK` / `SLAP-DEF`。

## 事件封包（8 bytes，little-endian）

| offset | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | `uint32` | `t_onset` | 动作起点，换算到裁判时钟的 ms |
| 4 | `uint8` | `cls` | 类别编号，见标头档 enum |
| 5 | `uint8` | `conf` | 信心度 × 255 |
| 6 | `uint16` | `peak` | gyro 模长峰值（dps），用来分力道 |

## 对时

1. 裁判每 1 s 写一次 Sync 特征值：`uint32 referee_ms`。
2. Nano 收到时记录 `offset = referee_ms − millis()`，对最近 8 次取中位数，滤掉 BLE jitter。
3. Nano 送事件时填 `t_onset = local_onset_ms + offset`。

量测项：对时残差（用 GPIO 同时触发两台 Nano，比对两边回报的 `t_onset` 差值），写进实验报告。
