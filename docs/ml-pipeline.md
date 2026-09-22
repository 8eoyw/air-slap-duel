# ML Pipeline

CSV → `ml/train.py` → int8 TFLite → `model.h` → Arduino（Chirale_TensorFLowLite）。
也可以把 `ml/` 上传到 Colab 执行，与课件流程相同。

## 数据采集

```bash
python tools/collect_session.py --port COM5 --subject s01 --role attacker --reps 50
python tools/collect_session.py --port COM5 --subject s01 --role defender --reps 50
python tools/collect_session.py --port COM5 --subject s01 --background 900
```

- 4 人以上，每类 50–100 次；类别顺序会被打乱，避免疲劳与标签相关。
- 每人 15 分钟 `background`：拨头发、挥手、推眼镜、走路、拿水杯。**没有这类，demo 时观众一动就误触。**
- 攻击和防守的 background 共用同一份录音（放在 `background/`，两个任务都会读）。
- 存放：`data/<受试者代号>/<类别>/<时间戳>.csv`，字段 `t_ms,ax,ay,az,gx,gy,gz`。

## 切窗：与固件一致

`slapml.trigger_points()` 重放固件的触发逻辑（`slap_node.h`）：
`|a| > acc_th` 或 `|g| > gyro_th`，冷却期内不再触发。窗口 = 触发点前 `SLAP_PRE` 个样本 + 之后的样本，共 `SLAP_WINDOW` 个。

- 动作 take：取第一个触发点的窗口；一直没越过阈值的 take 会被跳过并显示警告（代表阈值太高或动作太轻）。
- background take：每个会触发的点都取一个窗口，也就是只学「真的会送进模型」的那些乱动。
- 阈值、窗口、取样率都写进 `model.h`，固件直接采用，训练和上板不会不一致。

## 前处理与增强

- 归一化：训练集 per-channel mean/std，写进 `model.h`。
- 增强（只对训练集）：触发点抖动 ±100 ms、幅度缩放 0.8–1.2、高斯杂讯。
- class weight 平衡各类样本数。

## 切分

**依人切分**：默认最后一位受试者当 test、倒数第二位当 val，其他训练；`--test-subject` 可指定。
`--loso` 做留一人交叉验证（实验 4）。

## 模型

| `--model` | 结构 | 合成数据上 int8 大小 |
|---|---|---|
| `cnn`（默认） | Conv1D 16-32-32 + GAP | ~15 KB |
| `dense` | Flatten-32-16 | 8–13 KB（依窗口长度） |

tensor arena 预设 40 KB（`SLAP_ARENA_BYTES`），实际用量在节点开机时印出 `INFO,arena_used=`。

## 输出

`ml/out/<task>/`：`model_fp32.tflite`、`model_int8.tflite`、`model.h`、`report.json`（大小、准确率、混淆矩阵）、`confusion.png`。

> TF 2.16 自带的 Keras 3 转 TFLite 时 Conv1D 会失败，所以 `requirements.txt` 用 `tf_keras`，
> 并设置 `TF_USE_LEGACY_KERAS=1`（`train.py` 已自动设置）。
