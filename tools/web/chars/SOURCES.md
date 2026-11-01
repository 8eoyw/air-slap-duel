# 角色素材來源

`<角色>/<姿勢>.webp` 全部由 `tools/gen_chars.py` 生成：Antigravity CLI 的 image-generator 帶參考圖出關鍵姿勢，
在洋紅底上生成再做色鍵去背。提示詞和各姿勢描述都在該腳本裡；`manifest.json` 列出現有姿勢，頁面依此載入，
缺的姿勢沿 `FALLBACK` 退回。

參考圖取自 B 站 UP 主「奶史_」的 AI 影片，角色造型歸原作者，這裡只是課程項目的非商業使用：

| 角色 | 參考來源 |
|---|---|
| 奶蛙 | BV1t4uh6tENG 107.7s、BV1rQpg6oEPZ 155.7s |
| 奶茶 | BV1rQpg6oEPZ 74.0s |
| 奶狗 | BV14yto62ESb 9.5s、BV1udbN6GEya 26.8s |
| 豆奶 | BV1t4uh6tENG 100.2s |
| 奶蛋 | BV1rQpg6oEPZ 238s 全家福、BV14yto62ESb 12s |
| 奶粉、奶鼠 | BV1rQpg6oEPZ 238s 全家福、110s、170s |

奶粉、奶鼠的名字是從抖音標題「奶粉看奶鼠出丑了」推斷的，原作者沒有在這些影片裡明說。

`build/char_gen/` 放參考圖和生成原圖（不進 git）。
