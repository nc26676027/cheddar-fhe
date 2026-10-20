# Discrete digit representation prototypes: 2-bit digits and direct integer extraction (CPU)

**Spec:** `docs/specs/0006-discrete-meboot-latency.md`
**Status:** completed
**Blocked by:** 无（单次乘法耗时估算引用 02 的结果）

## 问题

要低于 Identity19，Discrete 独有部分得从约 520 ms 降到约 105 ms（约 4.5 倍）。现在的表示方式是 6 个 1-bit digit：6 个 degree-63 Remez 多项式加上 12 次 cubic/分量（cubic 只能收敛到 {0,1}）。靠 batching 和调参很难超过约 2 倍，所以要先用原型判断换表示方式值不值得做。

## 要做的

只在 CPU 上做原型（明文或模拟误差），不接 GPU 路径。两个候选：

- **(a) 3 个 2-bit digit**：3 个提取多项式，cleaning 换成能收敛到 {0,1,2,3} 的多项式
- **(b) 直接提取整数**：用一个多项式直接得到 I ∈ [-22, 21]，再用对多个整数都能收敛的多项式清理

每个候选回答：

1. 在 2^-33 整数残差门槛下（同时保证最终 < 2^-18），要多少次密文乘法、多少 level
2. 能不能放进现在的 6 + 2 个 level（L29→L21）。**超过 8 个 level 直接否掉**
3. 按 ② 的单次乘法耗时估算的 GPU 耗时，和现在的 6×1-bit 方案对比

## 验收

- 原型脚本放进 `tools/research/`，结论写进 `docs/research/discrete-identity/`
- 每个候选给出结论：继续做（再开实现 issue）还是放弃，并写明原因
- 如果决定换表示方式，另外评估要不要写 ADR
