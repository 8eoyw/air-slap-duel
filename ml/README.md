# ML

```bash
pip install -r requirements.txt
python train.py --data ../data --task attacker --window 1.0 --model cnn
python train.py --data ../data --task defender --window 1.0 --model cnn
python train.py --data ../data --task attacker --loso   # 留一人交叉验证（实验 4）
```

输出在 `out/<task>/`：`model_fp32.tflite`、`model_int8.tflite`、`model.h`（含归一化参数与类别表）、`report.json`。

也可以把 `train.py` 贴到 Colab 跑，与课件流程相同。详见 [docs/ml-pipeline.md](../docs/ml-pipeline.md)。
