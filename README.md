# 空气巴掌对决 Air Slap Duel

边缘计算课程项目。灵感来自 *ROSE & CAMELLIA COLLECTION*（The Slap Game）的搧巴掌对决，
但**没有人会被真的打到**：巴掌是戴在手上的 IMU 在设备端辨识出的手势，
「挨打」由屏幕表情、舵机与音效呈现。

## 系统一览

```
Nano 33 BLE Sense #1（攻击者，绑手背）──BLE: 8-byte 事件──┐
                                                         ├─→ 小智 ESP32-S3（裁判/擂台）
Nano 33 BLE Sense #2（防守者，头带或手腕）─BLE: 8-byte 事件─┘     状态机 · 时序仲裁 · LCD 脸 · 舵机
                                                                  └─(可选 Wi-Fi) 云端 LLM 嘲讽台词
```

| 角色 | 设备 | 辨识类别 |
|---|---|---|
| 攻击者 | Nano 33 BLE Sense #1 | `forehand` / `backhand` / `feint` / `background`，力道依 gyro 峰值分轻、中、重 |
| 防守者 | Nano 33 BLE Sense #2 | `dodge_left` / `dodge_right` / `block` / `background` |
| 裁判 | 小智 ESP32-S3 | 回合状态机、命中判定、扣血、表情、语音指令 |
| 物理回馈（加分） | 舵机 + 泡棉手 / 纸板头 | 重现「被打偏头」 |

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
  nano_attacker/            攻击端（有 model.h 用 TFLM，没有就用启发式分类）
  nano_defender/            防守端
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
| 裁判核心（判定表、状态机、假动作惩罚） | ✅ 单元测试通过 |
| nano_hub 备案 | ✅ 编译通过，未上板 |
| 小智 ESP-IDF 组件 + standalone | ✅ 写好，CI 用 ESP-IDF v5.4.1 编译 |
| xiaozhi_glue | ⚠️ 依 xiaozhi-esp32 v1.x–v2.x API 写的，需对照你们的版本 |
| ML pipeline（切窗、依人切分、LOSO、int8、扫描） | ✅ 合成数据上跑通 |
| 真实数据、阈值与力道校准、实验数据 | ☐ 需要硬件 |

## 分工

| 负责 | 范围 |
|---|---|
| A | 固件 / BLE / 对时 |
| B | ML pipeline / 数据 / 实验 |
| C | 裁判状态机 / LCD 脸 / 舵机 / 小智整合 |
| D | 报告 / demo 脚本 / 安全说明 |
