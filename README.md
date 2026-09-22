# 空气巴掌对决 Air Slap Duel

边缘计算课程项目。灵感来自 *ROSE & CAMELLIA COLLECTION*（The Slap Game）的搧巴掌对决，
但**没有人会被真的打到**：巴掌是戴在手上的 IMU 在设备端辨识出的手势，
「挨打」由屏幕表情、舵机与音效呈现。

## 系统一览

```
Nano 33 BLE Sense #1（攻击者，绑手背）──BLE: 8-byte 事件──┐
                                                         ├─→ 小智 ESP32-S3（裁判/擂台）
Nano 33 BLE Sense #2（防守者，头带或手腕）─BLE: 8-byte 事件─┘     状态机 · 时序仲裁 · LCD 脸 · TTS · 舵机
                                                                  └─(可选 Wi-Fi) 云端 LLM 嘲讽台词 / 笔电 dashboard
```

| 角色 | 设备 | 辨识类别 |
|---|---|---|
| 攻击者 | Nano 33 BLE Sense #1 | `forehand` / `backhand` / `feint` / `background`，力道依 gyro 峰值分轻、中、重 |
| 防守者 | Nano 33 BLE Sense #2 | `dodge_left` / `dodge_right` / `block` / `background`，APDS-9960 可作格挡的第二输入 |
| 裁判 | 小智 ESP32-S3 | 回合状态机、命中判定、扣血、表情、旁白、语音指令 |
| 物理回馈（加分） | 舵机 + 泡棉手 / 纸板头 | 重现「被打偏头」 |

**设计原则（边缘计算的核心论点）**

1. 推论全部在 Nano 上做，BLE 只传分类结果，不传 raw IMU。
2. 硬即时在本地，云端只加料：拔掉 Wi-Fi 游戏照样能玩。
3. 判定用节点本地时间戳（由裁判的 sync beacon 对时），不用封包到达时间。

## 目录

```
docs/                 设计文档
  architecture.md     架构与回合流程
  protocol.md         BLE 服务、事件封包、对时
  ml-pipeline.md      数据搜集 → 训练 → int8 → 上板
  experiments.md      报告要跑的量化实验
  timeline.md         10 周时程与分工
  safety.md           安全说明
  risks.md            已知风险与备案
firmware/
  common/             两端共用的协定标头档
  imu_collect/        IMU 数据搜集 sketch（输出 CSV）
  nano_attacker/      攻击端：阈值触发 → 推论 → BLE 通知
  nano_defender/      防守端
  referee_xiaozhi/    裁判端（小智固件上的扩充）
ml/                   训练、量化、导出 model.h
data/                 原始 CSV（依受试者分文件夹，不进 git 的大档见 .gitignore）
tools/                serial 即时显示、延迟量测脚本
```

## 快速开始

1. 确认 Nano 板子版本：Rev1 用 `Arduino_LSM9DS1`，Rev2 用 `Arduino_BMI270_BMM150`，
   在 `firmware/common/board.h` 切换。
2. 烧入 `firmware/imu_collect`，用 `tools/serial_to_csv.py` 录数据到 `data/<受试者>/<类别>/`。
3. 照 `ml/README.md` 训练并导出 `model.h`，拷贝到 `nano_attacker/` 或 `nano_defender/`。
4. 小智端见 `firmware/referee_xiaozhi/README.md`。

## 分工

| 负责 | 范围 |
|---|---|
| A | 固件 / BLE / 对时 |
| B | ML pipeline / 数据 / 实验 |
| C | 裁判状态机 / LCD 脸 / TTS / 舵机 |
| D | 报告 / demo 脚本 / 安全说明 |

## 状态

框架阶段。各文件夹中的 `TODO` 是待实作项目，进度见 [docs/timeline.md](docs/timeline.md)。
