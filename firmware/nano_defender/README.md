# 防守端

与 `nano_attacker` 同一份程序，只改：

```cpp
const SlapRole ROLE = ROLE_DEFENDER;
const char*    NAME = SLAP_NAME_DEFENDER;
```

并换成防守模型的 `model.h`（类别：background / dodge_left / dodge_right / block）。

可选：用 APDS-9960 的手势事件作为 `block` 的第二种输入（`Arduino_APDS9960` 函数库）。

TODO：两端稳定后，把共用逻辑抽成 `common/slap_node.h`，两个 sketch 只留设置。
