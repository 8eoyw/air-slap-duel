# 量化实验

这一节决定报告是研究还是玩具。每个实验都要有表格或图。结果放在 `results/`。

| # | 实验 | 自变量 | 量测 | 怎么跑 |
|---|---|---|---|---|
| 1 | 量化影响 | FP32 vs int8 | 模型大小、准确率、Nano 上单次推论时间 | `train.py` 的 `report.json`；上板后 `analyze_log.py latency` |
| 2 | 窗口 / 取样率扫描 | 窗口 0.5/0.75/1.0/1.5 s；50/100 Hz；cnn/dense | 准确率、模型大小、arena 用量 | `ml/sweep.py` → `sweep.csv`、`sweep.png`；arena 看节点的 `INFO,arena_used` |
| 3 | 边缘 vs 集中推论 | 节点推论 vs 串 raw | BLE 流量、封包遗失、端到端延迟、Nano 电流 | 节点加 `-DSLAP_STREAM_RAW` 编译，`ble_monitor.py --raw`；电流用 USB 电流表 |
| 4 | 跨用户泛化 | 留一人交叉验证 | 每人准确率、混淆矩阵 | `train.py --loso` → `loso.json`、`loso_confusion.png` |
| 5 | 对时精度 | 有 / 无 sync beacon | 两节点时间戳残差 | 两块板一起敲（同一个物理事件），`analyze_log.py sync` |
| 6 | 误触率 | 有 / 无 background 类、有 / 无阈值 | 每分钟误触 | 15 分钟自由活动录 log，`analyze_log.py falsetrig` |

## 实验 3 的对照组怎么编译

```bash
arduino-cli compile -b arduino:mbed_nano:nano33ble --library firmware/libraries/SlapCommon \
  --build-property "build.extra_flags=-DSLAP_STREAM_RAW" -u -p COM5 firmware/nano_attacker
python tools/ble_monitor.py --only SLAP-ATK --raw --seconds 60
```

预期：事件模式约 8 B/次，raw 模式 14 B × 100 Hz = 1.4 KB/s；raw 模式还要把推论搬到裁判端，这就是「为什么在边缘推论」的论据。
