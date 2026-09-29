# 空气巴掌对决 Air Slap Duel

边缘计算课程项目。灵感来自 *ROSE & CAMELLIA COLLECTION*（The Slap Game）的搧巴掌对决，
但**没有人会被真的打到**：巴掌是戴在手上的 IMU 在设备端辨识出的手势，
「挨打」由屏幕表情、舵机与音效呈现。

## 系统一览

```
Nano 33 BLE Sense P1（玩家 1，绑出手的手背）──BLE: 8-byte 事件──┐
                                                              ├─→ 小智 ESP32-S3（裁判/擂台）
Nano 33 BLE Sense P2（玩家 2，绑出手的手背）──BLE: 8-byte 事件──┘     状态机 · 时序仲裁 · LCD 脸 · 舵机
                                                                       └─(可选 Wi-Fi) 云端 LLM 嘲讽台词
```

每位玩家全程只戴一块板子，**不用每回合交换装备**。板子上同时烧攻击模型和防守模型，
一次触发两个都推论、都送出；裁判依当下谁在攻、谁在守决定采用哪个结果
（防守方要能反击，所以防守方的板子也得认得出巴掌）。

| 模型 | 辨识类别 | 用途 |
|---|---|---|
| 攻击模型 | `forehand` / `backhand` / `feint` / `background`，力道依 gyro 峰值分轻、中、重 | 攻击、假动作后的追击、防守方反击 |
| 防守模型 | `dodge_left` / `dodge_right` / `block` / `background` | 闪避、格挡 |

| 角色 | 设备 | 职责 |
|---|---|---|
| 玩家 ×2 | Nano 33 BLE Sense（`nano_player`） | 两条触发管线 + 两个模型，只送分类结果 |
| 裁判 | 小智 ESP32-S3 | 回合状态机、命中判定、扣血、表情、语音指令 |
| 物理回馈（加分） | 舵机 + 泡棉手 / 纸板头 | 重现「被打偏头」 |

> 旧的单角色板子（`nano_attacker` = `SLAP-ATK`、`nano_defender` = `SLAP-DEF`，每回合交换）保留，
> 用于数据采集和第 1–2 周的 BLE 联调；这种模式下没有反击。

## 游戏规则

照 *Rose & Camellia* 的四招：**巴掌、闪避、假动作、反击**。攻守轮流，HP 100 先归零者输。
时间一律用节点端的动作起点（对时后），不用封包到达时间。参数都在
[`slap_game_config.h`](firmware/libraries/SlapCommon/src/slap_game_config.h)。

**攻击阶段**：裁判宣布后攻击方有 5 s，时间到还没打出真巴掌就 `TIMEOUT`，直接换人（不再无限重新宣布）。

**一记巴掌的判定**（Δ = 防守动作起点 − 巴掌起点）

| 防守方 | 结果 | 防守方扣血 | 之后 |
|---|---|---|---|
| 没动作，或 Δ > 600 ms | `HIT` 命中 | 轻 10 / 中 20 / 重 30 | 换人 |
| Δ < 200 ms（含巴掌前 300 ms 内先动） | `JUMPED` 抢跑 | 同命中 | 换人 |
| 200–600 ms，`block` | `BLOCKED` 格挡 | 一半 | 换人，**不能反击** |
| 200–600 ms，闪错方向 | `GRAZE` 擦过 | 一半 | 换人 |
| 200–600 ms，闪对方向 | `MISS` 闪开 | 0 | **可以反击** |

格挡不看方向，所以只减半、不给反击；想全身而退还要反打，就得猜对正反手。这条让正反手辨识有意义。

**反击**：闪对方向后 150–800 ms 内，防守方打出正手或反手 → `COUNTERED`，攻击方吃反击的力道伤害 ×1.5；
如果被闪掉的是轻巴掌（出手太软），反击 ×2。没在窗口内反击就只算 `MISS`。

**假动作**：`feint` 不算攻击，攻击阶段继续。

| 防守方 | 攻击方接着 | 结果 |
|---|---|---|
| 没上当（窗口内没动） | — | 回到待攻，5 s 时限照算 |
| 上当（闪或挡） | 防守动作之后 1 s 内补一记真巴掌 | `FEINT_PUNISHED`：不可闪避，暴击 ×1.5 |
| 上当 | 没补 | `FEINT_FOOLED`：0 伤害，防守方下次防守窗口缩到 450 ms |
| 还没反应 | 就补了真巴掌 | 真巴掌照一般规则判定（防守方先前为假动作做的动作可能变成抢跑） |

**正反手方向**：右撇子攻击方的正手要往右闪、反手往左闪；左撇子攻击方左右对调。
开局时由裁判设定每位玩家的惯用手（`RefereeCore::setLeftHanded()`），不用重新编译。

**不做**：原作的弱点瞄准暴击（IMU 无法瞄准）、多段连击与 double slap（需要改成连续计时的攻击阶段，超出课程范围）。

**设计原则（边缘计算的核心论点）**

1. 推论全部在 Nano 上做，BLE 只传分类结果，不传 raw IMU。
2. 硬实时在本地，云端只加料：拔掉 Wi-Fi 游戏照样能玩。
3. 判定用节点本地时间戳（由裁判的 sync beacon 对时），不用封包到达时间。

## 目录

```
docs/                       设计文档（架构、协定、ML、实验、时程、安全、风险）
firmware/
  libraries/SlapCommon/     共用 Arduino 库：协定、节点运行时、可移植裁判核心
  imu_collect/              IMU 数据采集 sketch
  nano_player/              玩家端：攻击 + 防守两个模型（正式比赛用）
  nano_attacker/            单角色攻击端（有 model.h 用 TFLM，没有就用启发式分类）
  nano_defender/            单角色防守端
  nano_hub/                 备案：第三块 Nano 当 BLE central + 裁判，UART JSON 给小智
  referee_xiaozhi/
    components/slap_referee/  ESP-IDF 组件（NimBLE central 或 UART 模式）
    standalone/               独立测试固件：第 1–2 周验证 BLE 可行性
    xiaozhi_glue/             接进 xiaozhi-esp32 的胶水代码（表情、MCP 语音指令）
ml/                         训练、量化、导出 model.h、窗口扫描
tools/                      采集、BLE 监看、比赛 dashboard、实验分析、合成数据
tests/                      裁判核心的单元测试（PC 上跑）
data/                       原始 CSV（只用受试者代号）
```

## 快速开始

环境：[arduino-cli](https://arduino.github.io/arduino-cli/)、Python 3.11、ESP-IDF v5.4（只有小智端需要）。

```bash
# 1. Arduino 库
arduino-cli core install arduino:mbed_nano
arduino-cli lib install ArduinoBLE Arduino_BMI270_BMM150 Arduino_LSM9DS1 Chirale_TensorFLowLite

# 2. 确认板子版本：Rev1 在 firmware/libraries/SlapCommon/src/slap_board.h 把 NANO_REV 改成 1

# 3. 采集数据
arduino-cli compile -b arduino:mbed_nano:nano33ble --library firmware/libraries/SlapCommon -u -p COM5 firmware/imu_collect
pip install -r ml/requirements.txt
python tools/collect_session.py --port COM5 --subject s01 --role attacker --reps 50
python tools/collect_session.py --port COM5 --subject s01 --background 900

# 4. 训练并烧入
cd ml && python train.py --task attacker && cd ..
cp ml/out/attacker/model.h firmware/nano_attacker/
arduino-cli compile -b arduino:mbed_nano:nano33ble --library firmware/libraries/SlapCommon -u -p COM5 firmware/nano_attacker

# 5. 没有裁判时先用笔电看事件
python tools/ble_monitor.py
```

还没有数据时，可以先用合成数据把整条 pipeline 跑一遍：
`python tools/synth_data.py && cd ml && python train.py --task attacker --data ../data_synth`

小智端见 [firmware/referee_xiaozhi/README.md](firmware/referee_xiaozhi/README.md)。

## 进度

| 模块 | 状态 |
|---|---|
| 协定、节点运行时、启发式分类 | ✅ 写好，本机编译通过（Nano 33 BLE） |
| TFLM 分类（合成数据训练出的 model.h） | ✅ 编译通过；RAM 47%；**尚未上板实测** |
| 裁判核心 v1（判定表、状态机、假动作惩罚） | ✅ 单元测试通过 |
| 裁判核心 v2（格挡减半、反击、假动作追击、逾时换人、惯用手） | 🚧 实现中 |
| `nano_player` 双模型节点、`model.h` 加命名空间 | 🚧 实现中 |
| 训练时攻守两任务互为 background（防止闪避被认成巴掌） | 🚧 实现中 |
| nano_hub 备案 | ✅ 编译通过，未上板 |
| 小智 ESP-IDF 组件 + standalone | ✅ 写好，CI 用 ESP-IDF v5.4.1 编译 |
| xiaozhi_glue | ⚠️ 依 xiaozhi-esp32 v1.x–v2.x API 写的，需对照你们的版本 |
| ML pipeline（切窗、依人切分、LOSO、int8、扫描） | ✅ 合成数据上跑通 |
| 真实数据、阈值与力道校准、实验数据 | ☐ 需要硬件；**先量两个模型的混淆矩阵与延迟当 baseline，再上 v2 规则** |
| `DODGE_WINDOW_MIN_MS = 200` 是否误判反应快的人 | ☐ 待实测（人类视觉反应约 200–250 ms），并入实验 5 |

## 分工

| 负责 | 范围 |
|---|---|
| A | 固件 / BLE / 对时 |
| B | ML pipeline / 数据 / 实验 |
| C | 裁判状态机 / LCD 脸 / 舵机 / 小智整合 |
| D | 报告 / demo 脚本 / 安全说明 |
