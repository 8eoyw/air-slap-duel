# 角色素材來源

全部取自 B 站 UP 主「奶史_」的 AI 影片（720p），以 rembg isnet-general-use 去背（tools/cut_chars.py，參數見 spec.json）。

| 角色 | 狀態 | 影片 | 時間 |
|---|---|---|---|
| 奶蛙 | still_0 | BV1t4uh6tENG 選擇奶蛙還是豆奶 | 108.0s |
| 奶蛙 | laugh_0..12 捧腹大笑 | BV1rQpg6oEPZ 奶家人躲猫猫大赛-深山城堡篇 | 155.7–158.15s |
| 奶茶 | laugh_0..7 | BV1rQpg6oEPZ | 74.0–74.7s |
| 奶狗 | still | BV14yto62ESb 選擇奶蛋還是奶狗 | 9.5s |
| 奶狗 | laugh_0..4 | BV1udbN6GEya 巴甫奶夫的奶狗 | 26.8–27.3s |
| 豆奶 | still | BV1t4uh6tENG | 100.2s |
| 豆奶 | laugh_0..4 | BV1rQpg6oEPZ 全家福 | 239.75–242.75s |

重新產生：`yt-dlp` 下載到 vids/，`rembg[cpu]` 的 venv 執行 `python tools/cut_chars.py tools/web/chars/spec.json tools/web/chars --qa qa.jpg`。
