# 风险与备案

| 风险 | 影响 | 何时验证 | 备案 |
|---|---|---|---|
| 小智固件无法当 BLE central 同时连两台 Nano（Wi-Fi + 音频串流 + BLE 共存，RAM 吃紧） | 架构要改 | 第 1–2 周 | `firmware/nano_hub`：第三块 Nano 当 central 并跑裁判，UART JSON 给小智（课件的双板架构） |
| Nano 版本不同：Rev1 为 LSM9DS1，Rev2 为 BMI270+BMM150 | 范例 sketch 要改 | 第 1 周 | `slap_board.h` 的 `NANO_REV` 切换函数库 |
| 一组只有 2 台 Nano | 无法同时攻守 | 第 1 周 | 向助教多借一台；或攻守轮流换手戴 |
| BLE 连接间隔造成 jitter | 判定不公平 | 第 5 周 | 本地时间戳 + 对时（见 protocol.md）；连接间隔设 7.5–15 ms |
| 跨用户准确率低 | demo 翻车 | 第 4 周 | 增加受试者；demo 前让评审试挥 3 次做少量校准 |
| 误触 | demo 翻车 | 第 7 周 | background 类 + 加速度阈值 + 信心度门槛 + 冷却期 |
