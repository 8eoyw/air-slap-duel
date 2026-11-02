# 组装 / 训练 / 数据通路 实施清单

本文把仓库里的代码、参数与文档收成三条可执行的线：**装硬件 → 训模型 → 通数据**。
每条都给出物料、接线、命令、预期日志与验收标准。标 ⚠️ 的是必须按你的实物实测/调整的地方。

一句话架构：**两块 Nano 33 BLE Sense 戴在手背做端侧推论，小智 ESP32-S3 当裁判；BLE 上只跑 8 字节事件，不传 raw IMU。**

---

## 0. 先选拓扑（决定买几块板）

| 方案 | 谁连 BLE | 谁跑裁判 | 用途 | 板子数 |
|---|---|---|---|---|
| **A 玩家板**（正式） | 小智 ESP32-S3 | 小智 | 比赛、demo、正式录像 | 2 Nano + 1 ESP32-S3 |
| **B 单角色板**（过渡） | 小智 / nano_hub | 小智 / nano_hub | 第 1–2 周 BLE 联调、数据采集 | 2 Nano + 1 ESP32-S3 |
| **C UART 备案** | 第三块 Nano（nano_hub） | nano_hub | 小智 Wi-Fi + 音频 + BLE 共存失败时 | 3 Nano + 1 ESP32-S3 |

- A 与 B 的差别只在固件：`nano_player`（双模型、不换装备）vs `nano_attacker` / `nano_defender`（单模型、每回合交换）。
- 决策点在第 1 周：先按 A 验证「小智同时连两台 Nano」；连不上、heap 掉、频繁断线 → 立刻转 C，不要拖到第 6 周。
- 数据采集用 B 或 A 都行，但**绑法必须和比赛一致**（见 1.2）。

---

## 1. 硬件组装

### 1.1 物料清单（BOM）

| 类别 | 物料 | 规格 / 型号 | 数量 | 说明 |
|---|---|---|---|---|
| 玩家节点 | Arduino Nano 33 BLE Sense | **Rev1（LSM9DS1）** 或 Rev2（BMI270+BMM150） | 2 | 两块必须同版本；仓库默认 Rev1 |
| 裁判 | 小智 ESP32-S3 板 | 带屏、麦克风、扬声器、8 MB PSRAM | 1 | 实测 Goouuu ESP32-S3-CAM 可用；优先选 xiaozhi-esp32 支持列表内的板 |
| 物理回馈 | 舵机 | SG90 / MG90S 一类 hobby servo | 1 | 50 Hz、0.5–2.5 ms 脉宽 |
| 物理回馈 | 道具 | 泡棉手 / 纸板头 | 1 | 只打道具，不朝人 |
| 电源 | 舵机电源 | 独立 5 V / 2 A | 1 | **不可**从 ESP32 板 5 V 取电 |
| 电源 | 充电宝 | 5 V / 1 A 以上，带开关 | 2 | 每个玩家一个；放臂袋或腰包 |
| 穿戴 | 弹力带 + 魔术贴 | 25–38 mm 宽 | 2 套 | 或运动护腕 + 3M Dual Lock |
| 线材 | micro-USB 数据线 | 必须能传数据，20–30 cm | 2 | 采集时接 PC，比赛时接充电宝 |
| 线材 | USB-C 线 | 数据 + 供电 | 1 | 裁判板 |
| 烧录 | USB-TTL（CH340） | 3.3 V 电平 | 1 | 裁判板原生 USB 不枚举时用 |
| 耗材 | 杜邦线、热缩管、扎带 | — | 若干 | 舵机与 UART 接线 |
| 场地 | 地面胶带 / 标记 | — | 1 卷 | 两位玩家 1.5 m 线 |
| 可选 | 第三块 Nano 33 BLE | 同版本 | 1 | C 方案 nano_hub |
| 可选 | USB 电流表 | — | 1 | 实验 3「边缘 vs 集中」的电流数据 |
| 可选 | 投影 / 大屏 + 笔电 | HDMI | 1 | 跑 `web_dashboard.py` |

### 1.2 两个玩家节点：机械与电气

- [ ] **位置**：板子绑在**出手那只手的手背**（掌指关节后方 3–5 cm），长边顺着前臂方向。
- [ ] **一致性**：攻击动作和防守动作都用**同一个位置、同一条绑带松紧**录制。旧设计把防守板放头带/手腕，那份数据不能混用。
- [ ] **固定**：弹力带压住板子两端，USB 口朝手腕，线沿前臂用扎带固定，挥击 20 次不移位。
- [ ] **天线**：板端天线区不要贴金属、不要被手掌完全包住；BLE 断线先查这里。
- [ ] **标识**：一块贴 `P1`、一块贴 `P2`，固件也分别烧 `-DSLAP_PLAYER=1` / `=2`，别只靠记忆。
- [ ] **供电**：充电宝 → micro-USB；电池袋固定在臂外侧，避免甩动拉扯接口。
- [ ] **版本确认**：Rev1 用 `Arduino_LSM9DS1`；Rev2 用 `Arduino_BMI270_BMM150` 并把 `slap_board.h` 的 `NANO_REV` 改成 2（或编译加 `-DNANO_REV=2`）。
- [ ] **不混版本**：采集用哪块板，比赛就用哪块板。Rev1 与 Rev2 的 IMU 量程/零偏不同，模型会掉准确率。

### 1.3 裁判节点（小智 ESP32-S3）

- [ ] 先烧官方 xiaozhi-esp32 固件，确认**屏幕、麦克风、扬声器、Wi-Fi** 都正常，再动 slap 组件。
- [ ] 烧录口：先试原生 USB（`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`）。电脑认不到 → 改插板上 TTL 口（CH340），装 WCH CH341SER 驱动，用 `ttl.defaults` 把 console 切到 UART。
- [ ] **Windows 路径**：ESP-IDF 在含中文的路径会报 `filesystem error: Cannot convert character sequence`，`subst` 也无效。把 `firmware/` 复制到 `C:\esp\slap_build\firmware` 再 build。
- [ ] 摆放：屏幕朝观众，麦克风朝玩家（语音指令 1–2 m 内），避免和路由器叠放。
- [ ] 供电：USB 5 V；舵机另供 5 V，**必须共地**。

### 1.4 舵机与道具接线

| 舵机线 | 接到 | 备注 |
|---|---|---|
| 信号（橙/黄） | ESP32-S3 一个空闲 GPIO | `idf.py menuconfig` → Air Slap Duel referee → `SLAP_SERVO_GPIO`，默认 `-1` 表示不接 |
| V+（红） | 独立 5 V / 2 A 电源正 | 不要接板子的 5 V |
| GND（棕/黑） | 电源负 **+** ESP32-S3 GND | 不共地会抖、会乱摆 |

- [ ] 先**空载**测角度：rest 60°，命中 +25 / +45 / +70°，350 ms 回位；确认不撞机械限位。
- [ ] 再装泡棉手/纸板头，限位留 10° 余量。
- [ ] 软件用 LEDC timer 2 / channel 5（避开 LCD 背光 PWM），14 bit、50 Hz。
- [ ] 安全：舵机扭力小、有限位、只作用在道具上，不朝向人。

### 1.5 UART 备案接线（仅 C 方案）

| nano_hub | ESP32-S3 | 说明 |
|---|---|---|
| `D1`（Serial1 TX） | `GPIO18`（`SLAP_UART_RX_GPIO`） | JSON 事件，必须接 |
| `D0`（Serial1 RX） | `GPIO17`（`SLAP_UART_TX_GPIO`） | 指令回传，要 dashboard 控场就接 |
| `GND` | `GND` | 必须共地 |

115200 8N1，一行一个 JSON。只做单向展示时可只接 TX→RX + GND。

### 1.6 场地

- [ ] 两位玩家间距 **≥ 1.5 m**，地上贴线；全程对空挥击，无身体接触。
- [ ] 笔电跑 `python tools/web_dashboard.py --port COMx`，HDMI 接投影；浏览器全屏（F）。
- [ ] 彩排时把「拔 Wi-Fi 继续玩」这一步真的走一遍。

### 1.7 上电自检（预期日志）

| 位置 | 预期输出 | 说明 |
|---|---|---|
| Nano 串口 115200 | `INFO,name=SLAP-P1,role=2,window=100,hz=100` | 角色 2 = 玩家板 |
| Nano 串口 | `INFO,arena_used=6084`（每个模型一行） | 实际用量；arena 预留 12 KB |
| Nano 串口 | `SYNC,<offset>,<median>` 每秒一次 | 已对时 |
| 裁判 console | `SLAP-P1 ready` / `SLAP-P2 ready` | 两台都出现 |
| 裁判 console | `conn 0 interval 7.50 ms` | 期望 7.5–15 ms |
| 裁判 console | `free internal heap` 每 10 s | 参考 ~272 KB 稳定 |
| 挥手 | `TRIG,...` 然后 `EVT,...` | 触发到送出 |
| dashboard | `announce` → `event` → `result` | 完整回合 |

---

## 2. 训练模型

### 2.1 环境

```bash
# 1) Arduino 端
arduino-cli core install arduino:mbed_nano
arduino-cli lib install ArduinoBLE Arduino_BMI270_BMM150 Arduino_LSM9DS1 Chirale_TensorFLowLite

# 2) PC 端（必须 Python 3.11；TF 2.16 没有 3.13 的轮子）
python -m venv .venv
.venv/Scripts/activate            # Windows
pip install -r ml/requirements.txt

# 3) 裁判端（只有小智需要）
#    ESP-IDF v5.4
```

`ml/requirements.txt` 固定 `tensorflow-cpu==2.16.2` + `tf_keras==2.16.0` + `numpy<2`。
`train.py` 已自动设 `TF_USE_LEGACY_KERAS=1`——Keras 3 转 TFLite 时 Conv1D 会失败，不要手动改回 Keras 3。

### 2.2 采集设计（决定模型上限）

| 任务 | 类别 | 动作要点 | 每人次数 |
|---|---|---|---|
| attacker | `forehand` | 正手搧，掌心朝外、由外往内 | 50–100 |
| attacker | `backhand` | 反手搧，手背由内往外 | 50–100 |
| attacker | `feint` | 起手像要搧，中途收回 | 50–100 |
| defender | `dodge_left` | 头往左闪 | 50–100 |
| defender | `dodge_right` | 头往右闪 | 50–100 |
| defender | `block` | 抬手格挡 | 50–100 |
| 两者共用 | `background` | 拨头发、挥手、推眼镜、走路、拿水杯 | 每人 15 min |

- [ ] **≥ 4 位受试者**。代码硬性要求 ≥ 3 人才能切分（train/val/test），LOSO 也要 ≥ 3 人；4 人以上才有泛化意义。
- [ ] 每人每类 50–100 次；`collect_session.py` 会打乱类别顺序，避免疲劳和标签相关。
- [ ] 每人 **15 分钟 background**（`--background 900`）。没有这一类，demo 时观众一动就误触。
- [ ] 攻击和防守共用同一份 background 录音，放在 `background/`，两个任务都会读。
- [ ] 只用代号 `s01`、`s02`…，不写姓名、不录影像（repo 是公开的）。
- [ ] 左撇子单独标注：比赛时裁判用 `left1` / `left2` 镜像正反手方向；训练集最好包含左撇子，否则方向特征会偏。

采集命令：

```bash
python tools/collect_session.py --port COM5 --subject s01 --role attacker --reps 50
python tools/collect_session.py --port COM5 --subject s01 --role defender --reps 50
python tools/collect_session.py --port COM5 --subject s01 --background 900
```

交互：倒计时后做一次动作，一个 take 2.5 s；回车保留，`r` 重录，`q` 结束。

### 2.3 目录与 CSV 规范

```
data/<受试者代号>/<类别>/<时间戳>.csv
```

- 表头：`t_ms,ax,ay,az,gx,gy,gz`
- 加速度单位 g，角速度单位 dps，固定 100 Hz（`t_ms` 来自 `millis()`）
- `data_synth/` 是合成数据，**不要混进 `data/`**
- 整包超过 50 MB 放网盘，repo 里留链接

采集质量抽检：

- [ ] 行数 ≈ 250（2.5 s × 100 Hz）
- [ ] `t_ms` 相邻差值 ≈ 10 ms，没有大段跳变
- [ ] 静止时 `az ≈ 1.0`，`ax/ay ≈ 0`

### 2.4 训练

```bash
cd ml
python train.py --task attacker
python train.py --task defender
```

默认配置（`slapml.Config`）：

| 项 | 默认 | 说明 |
|---|---|---|
| 窗口 | 1.0 s（100 样本） | 触发点前 50 + 后 50 |
| 取样率 | 100 Hz | 与固件 `SAMPLE_HZ` 一致 |
| 模型 | `cnn`（Conv1D 16-32-32 + GAP） | `dense` 更小 |
| epochs | 80 | — |
| 增强 | 触发点抖动 ±100 ms、幅度 0.8–1.2、高斯噪声 | 只对训练集 |
| 交叉负样本 | `cross_negatives=True` | 攻击模型把闪避/格挡当 background，反之亦然 |
| 切分 | 最后一位 test、倒数第二 val | `--test-subject` 可指定 |
| 类别权重 | 自动平衡 | — |

- [ ] 先跑合成数据把管线走通：
      `python tools/synth_data.py && python train.py --task attacker --data ../data_synth`
- [ ] 真实数据训练后跑 LOSO：`python train.py --task attacker --loso`
- [ ] 阈值不合适时用 `--acc-th` / `--gyro-th` 覆盖，导出的 `model.h` 会把阈值一起带下去。

输出 `ml/out/<task>/`：

| 文件 | 用途 |
|---|---|
| `model_fp32.tflite` | 对照，实验 1 |
| `model_int8.tflite` | 上板用 |
| `model.h` | 直接编进固件（含窗口、阈值、mean/std、类别 id） |
| `report.json` | 大小、准确率、混淆矩阵、切分 |
| `confusion.png` | 报告插图 |

### 2.5 导出与上板

```bash
cp ml/out/attacker/model.h firmware/nano_player/attacker_model.h
cp ml/out/defender/model.h firmware/nano_player/defender_model.h
```

- [ ] **两个 model.h 都要有**。`nano_player` 用 `__has_include` 判断，缺一个就整体回退启发式（认不出正反手）。
- [ ] 两个模型必须同取样率，否则 `static_assert(slap_model_attacker::kHz == slap_model_defender::kHz)` 编译失败。
- [ ] arena 每个模型 12 KB（实测用 ~6 KB）。换更大模型导致 `allocate_tensors` 失败时，调大 `SLAP_TFLM_INSTANCE` 的第三个参数。
- [ ] 置信度门槛在 `nano_player.ino`：攻击 180、防守 170；误触发多就调高，漏判多就调低。

```bash
# P1
arduino-cli compile -b arduino:mbed_nano:nano33ble --library firmware/libraries/SlapCommon \
  -u -p COM5 firmware/nano_player

# P2
arduino-cli compile -b arduino:mbed_nano:nano33ble --library firmware/libraries/SlapCommon \
  -u -p COM6 --build-property "compiler.cpp.extra_flags=-DSLAP_PLAYER=2" firmware/nano_player
```

单角色板则把 `model.h` 放到 `nano_attacker/` 或 `nano_defender/` 目录下即可。

### 2.6 验收指标

| 指标 | 目标 | 怎么量 |
|---|---|---|
| int8 测试准确率 | ≥ 0.85（合成数据的 0.99 是虚高，不作数） | `report.json` |
| 最差类别 recall | `feint` 不掉队 | `confusion.png` |
| 跨人泛化 | LOSO 均值 ≥ 0.8，标准差 < 0.1 | `train.py --loso` |
| 上板推论时间 | 记录 `EVT` 第 6 栏 `infer_us`；明显 > 80 ms 要查 arena/频率 | 串口日志 |
| 误触率 | 自由活动 < 1 次/分钟 | `analyze_log.py falsetrig` |
| 端到端延迟 | `rx − t_onset` 500–560 ms | `ble_monitor.py --log` + `analyze_log.py latency` |
| 资源 | RAM ~40%、flash ~63%（双模型） | 编译输出 |

### 2.7 常见坑

| 现象 | 原因 | 处理 |
|---|---|---|
| 训练报 `skipped_takes` 很多 | 动作太轻，从没越过触发阈值 | 加大动作幅度，或降 `--acc-th/--gyro-th` |
| `feint` 被认成 `forehand` | 假动作没有真的「收回」 | 重录时明确收手；增加不同起手速度的样本 |
| 闪避被认成巴掌 | 没开交叉负样本 | 确认 `cross_negatives=True` |
| 上板全是 background | mean/std 或类别 id 不匹配 | 看 `REJ` 行的 cls/conf；重新导出 model.h |
| `allocate_tensors` 失败 | arena 不够 | 加大 `SLAP_TFLM_INSTANCE` 的 arena |
| Conv1D 转 TFLite 报错 | 用了 Keras 3 | 用 `tf_keras`，保留 `TF_USE_LEGACY_KERAS=1` |

---

## 3. 数据通路

### 3.1 全景

```
训练期
  IMU 100 Hz ──USB CDC 115200──> imu_collect.ino ──CSV──> collect_session.py
                                                              │
                                                              v
                                                   data/<人>/<类>/*.csv
                                                              │
                                                    train.py（切窗/增强/int8）
                                                              │
                                                       model.h ──> 固件

比赛期
  IMU 100 Hz ─> 环形缓冲 100×6 ─> 阈值触发 ─> 收满窗口 ─> TFLM int8
                                                              │
                                        8 B 事件（t_onset,cls,conf,peak）
                                                              │ BLE notify
                                                              v
                                    小智 ESP32-S3（NimBLE central ×2）
                                      │ 对时 beacon 1 Hz（反向）
                                      │ 状态机 + 判定
                                      v
                         JSON 行 ─> dashboard / 小智表情·TTS·舵机
                                      └─(可选 Wi-Fi) 云端 LLM 嘲讽
```

### 3.2 通路 A：采集（IMU → USB → CSV）

1. `imu_collect.ino` 固定 100 Hz 输出 `millis(),ax,ay,az,gx,gy,gz`；IMU 没有新样本就复用上一笔，保证时基不乱。
2. PC 发 `r` 开始、`s` 停止；`collect_session.py` 每次录 2.5 s。
3. 存成 `data/<subject>/<class>/<timestamp>.csv`，带表头。

- [ ] 串口 115200，能收到 `# ready rev=1 hz=100`
- [ ] 采 1 个 take 后文件行数 ≈ 250
- [ ] 同一个 take 里静止段 `az ≈ 1`

### 3.3 通路 B：比赛（IMU → 事件 → 裁判）

1. 100 Hz 采样进环形缓冲（100 样本 × 6 通道）。
2. 每个样本检查触发：`|a| > acc_th` 或 `|g| > gyro_th`；冷却 400 ms。
   - 攻击管线：2.5 g / 600 dps（`slapml.TRIGGERS`，写进 `model.h`）
   - 防守管线：1.6 g / 250 dps
3. 触发后还要收 `window − pre = 50` 个样本，也就是 **500 ms**，才够一个完整窗口。
4. 归一化（per-channel mean/std，随 `model.h` 下发）→ int8 推论。
5. `background` 或 `conf < conf_min` → 只打印 `REJ`，不发包。
6. 组 8 字节事件：`t_onset`（已换算到裁判时钟）、`cls`、`conf×255`、`peak`（dps）。
7. BLE notify；连接间隔请求 7.5–15 ms，实际由 central 决定。
8. 裁判按 `Δ = 防守起点 − 攻击起点` 判定；`BLE_GRACE_MS = 600` 用来等迟到的事件。

时延预算：

| 段 | 典型值 | 说明 |
|---|---|---|
| 触发点 → 窗口收满 | 500 ms | (100−50)/100 Hz |
| 推论 | 待实测（`EVT` 的 `infer_us`） | 目标远小于 100 ms |
| BLE 传输 | 7.5–15 ms 间隔 + 1 包 | 主动扫描、名称在 scan response |
| **合计 `rx − t_onset`** | **504–527 ms（实测）** | 宽限 600 ms 必须大于它 |

> 关键：判定只看 `t_onset` 差值，不看封包到达时间。所以 500 ms 延迟只影响「多久看到结果」，不影响判得对不对。

判定窗口（防守方相对攻击起点）：

| 时间 | 结果 |
|---|---|
| 攻击前 300 ms 内先动 | `JUMPED` 抢跑，按命中算 |
| < 200 ms | 同上 |
| 200–600 ms 且 `block` | `BLOCKED`，伤害减半，不能反击 |
| 200–600 ms 闪错方向 | `GRAZE`，伤害减半 |
| 200–600 ms 闪对方向 | `MISS`，0 伤害，开反击窗口 150–800 ms |
| 没动作 / > 600 ms | `HIT`，轻 10 / 中 20 / 重 30 |

### 3.4 通路 C：对时

1. 裁判每 1 s 写一次 Sync（`uint32 referee_ms`）。
2. Nano 算 `offset = referee_ms − millis()`，保留最近 8 次，取**中位数**。
3. 送事件时 `t_onset = onset + median_offset`。
4. 重连时清空历史（换了 central 就是换了时钟）。

- [ ] `SYNC` 行的 median 在 ±20 ms 内稳定
- [ ] 两块板同时敲一下，`analyze_log.py sync a.log b.log` 残差 < 30 ms

### 3.5 通路 D：raw 对照（实验 3）

```bash
arduino-cli compile -b arduino:mbed_nano:nano33ble --library firmware/libraries/SlapCommon \
  --build-property "build.extra_flags=-DSLAP_STREAM_RAW" -u -p COM5 firmware/nano_attacker
python tools/ble_monitor.py --only SLAP-ATK --raw --seconds 60
```

- 事件模式：8 B / 次
- raw 模式：14 B × 100 Hz ≈ 1.4 KB/s / 节点
- 这就是「为什么要在边缘推论」的量化论据；顺带用 USB 电流表量两种模式的功耗。

### 3.6 通路 E：UART 备案（C 方案）

`nano_hub` 跑裁判核心，USB 和 `Serial1` 同时输出同样的 JSON 行；小智只做渲染。
指令一行一个：`start`、`announce_done`、`swap_done`、`reset`、`left1`、`right1`、`left2`、`right2`。

### 3.7 联调顺序（严格按此顺序，别跳）

- [ ] 1. 无硬件：`make -C tests test`（裁判核心单测）、`make -C tests sim keys`、`python tools/web_dashboard.py --keys`
- [ ] 2. 单块 Nano：烧 `imu_collect`，能出 CSV
- [ ] 3. 单块 Nano：烧 `nano_player`（P1），`python tools/ble_monitor.py` 能看到 `EVT`
- [ ] 4. 裁判 standalone：`idf.py set-target esp32s3 && idf.py build flash monitor`，连上 P1
- [ ] 4b. 没有选手板：控制台 `ev <0|1> <class> <peak>` 注入，或 `python tools/bench_referee.py --port COMx`
- [ ] 5. 双板：P1 + P2 同时连；按 BOOT 开局，`python tools/dashboard.py --port COMx`
- [ ] 6. 对时：看 `SYNC`，两板同敲验残差
- [ ] 7. 舵机：先空载，再装道具
- [ ] 8. 小智 glue：语音「开始巴掌对决」，再走一遍第 5 步
- [ ] 9. 拔 Wi-Fi：确认判定与画面照常
- [ ] 10. 让没受过训的人乱挥 1 分钟，确认没有误触

### 3.8 故障对照表

| 症状 | 可能原因 | 处理 |
|---|---|---|
| 裁判扫不到板子 | 名称在 scan response，central 用了被动扫描 | 改主动扫描；确认 Nano 在 advertise |
| 连上又断 | 供电不足 / Wi-Fi 共存 heap 紧张 / 距离远 | 换供电；看 `free heap`；转 UART 方案 |
| 所有防守都判 `HIT` | 事件晚于判定窗口 | 确认 `BLE_GRACE_MS=600`；量 `rx−t_onset` |
| `t_onset` 乱跳 | 没对时 / 重连没清 offset | 看 `SYNC`；重连后等 8 个 beacon |
| 一直 `REJ` | 置信度门槛太高 / 模型没烧进去 | 看 `REJ` 的 cls/conf；确认两个 model.h 都在 |
| dashboard 空白 | 波特率不对 / 端口被占 / 没发 `status` | 115200；换口；重开页面 |
| ESP-IDF 构建报字符转换错 | 路径含中文 | 复制到 `C:\esp\...` |
| 原生 USB 认不到板 | OTG 口不枚举 | TTL 口 + CH340 + `ttl.defaults` |
| 舵机抖 / 乱摆 | 没共地 / 从板子取电 | 独立 5 V，共地 |

---

## 4. 参数速查

| 参数 | 值 | 位置 |
|---|---|---|
| 取样率 | 100 Hz | `slap_board.h` |
| 窗口 / 前置 | 100 / 50 样本（1 s / 0.5 s） | `model.h` |
| 攻击触发 | 2.5 g / 600 dps | `slapml.TRIGGERS` |
| 防守触发 | 1.6 g / 250 dps | 同上 |
| 冷却 | 400 ms | `model.h` |
| 置信度门槛 | 攻 180 / 守 170 | `nano_player.ino` |
| 防守窗口 | 200–600 ms | `slap_game_config.h` |
| 抢跑回溯 | 300 ms | 同上 |
| 反击窗口 | 150–800 ms | 同上 |
| 假动作追击 | 1000 ms | 同上 |
| BLE 宽限 | 600 ms | 同上 |
| 攻击阶段 | 5000 ms | 同上 |
| 力道门槛 | 400 / 800 dps | 同上（待校准） |
| 伤害 | 10 / 20 / 30 | 同上 |
| 对时 | 1 Hz，8 次中位数 | `slap_node.h` |
| 事件包 | 8 B | `slap_protocol.h` |
| raw 包 | 14 B | 同上 |
| 舵机 | 50 Hz，rest 60°，+25/45/70° | `slap_servo.cc` |

---

## 5. 与 10 周排程的对应

| 周 | 本清单对应 |
|---|---|
| 1–2 | §1 全部 + §3.7 的 1–5 步 + §2.2 试采 |
| 3–4 | §2.3–2.5 攻击数据与第一版模型 + §2.6 上板延迟 |
| 5 | §2.4 防守模型 + §3.4 对时 + §3.3 时延 |
| 6 | 裁判状态机、LCD 脸、TTS（本清单外） |
| 7 | 集成 + 补 background + §3.8 排障 |
| 8 | §1.4 舵机 + 语音必杀 + LLM 嘲讽 |
| 9 | §2.6 全部实验 + 报告 |
| 10 | §1.6 场地彩排 + 备援 |

---

## 6. 完工定义（Definition of Done）

- [ ] 两块玩家板连续戴 30 min 不掉线、不滑动，`rx − t_onset` 稳定在 500–560 ms
- [ ] 两个模型都在板上跑，`arena_used` 各 ~6 KB，RAM/flash 有余量
- [ ] 双模型混淆矩阵里 `feint` 与 `forehand` 不互混，闪避不被认成巴掌
- [ ] 四人 LOSO 均值 ≥ 0.8，最差一人不低于 0.7
- [ ] 自由活动 15 min 误触 < 15 次
- [ ] 裁判端完整跑完一局：`announce` → `event` → `result` → `swap` → `game_over`
- [ ] 拔掉 Wi-Fi，判定、表情、舵机、记分照常
- [ ] 未受训的人乱挥 1 分钟不触发有效攻击
- [ ] 现场 1.5 m 隔离线、道具无硬边、舵机限位确认
