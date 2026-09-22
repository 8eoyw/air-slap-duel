# ML

```bash
pip install -r requirements.txt
python train.py --task attacker                 # 依人切分，导出 out/attacker/model.h
python train.py --task defender
python train.py --task attacker --loso          # 留一人交叉验证（实验 4）
python sweep.py --task attacker                 # 窗口 / 取样率扫描（实验 2）

# 还没有真实数据：
python ../tools/synth_data.py && python train.py --task attacker --data ../data_synth
```

`slapml.py` 是共用逻辑（读取、与固件一致的切窗、模型、TFLite 导出、model.h 生成）。
详见 [docs/ml-pipeline.md](../docs/ml-pipeline.md)。
