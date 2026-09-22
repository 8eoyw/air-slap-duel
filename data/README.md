# 数据

```
data/<受试者代号>/<类别>/<时间戳>.csv
```

- **这个 repo 是公开的**：受试者只用代号（s01、s02…），不写姓名，不放照片或影片。
- 类别文件夹名称：`forehand` `backhand` `feint` `dodge_left` `dodge_right` `block` `background`。
- 用 `tools/collect_session.py` 采集，会自动放到正确位置。
- 小量 CSV 可以进 git；整包数据超过 50 MB 时改放网盘，在这里留链接。
- `data_synth/`（合成数据）不进 git，也不要混进这里。
