# Discrete MEBOOT 性能优化记录（2026-10-19）

本轮目标是确认 Discrete MEBOOT 的实际瓶颈，并验证 `DiscreteIntegerCleaner`
中 cubic 清理步骤的单变量优化。结论是：degree-63 DigitExtract 已经使用共享
basis 的 Paterson-Stockmeyer 结构，本轮没有改动 DigitExtract；改动集中在每个
digit 的 cubic polynomial evaluation。

## 当前 profiling 结论

以原始库的首次 profiling 为参考，完整 Discrete MEBOOT 的 GPU 中位数为
633.558 ms。阶段占比最高的是：

| 阶段 | GPU 中位数 |
| --- | ---: |
| DigitExtract，实部和虚部 | 338.037 ms |
| Cleaning，实部和虚部 | 131.770 ms |
| MEBOOT low CtS | 73.215 ms |
| Accurate bypass and alignment | 57.686 ms |

因此最大的后续方向仍然是 DigitExtract 的 basis 和六个输出树的成本拆分，而
不是重新启用 PS 优化：`EvalPolyBatch` 已经先构造共享 basis，再计算六个输出
多项式。

此前详细 stage profile 没有完整输出；`unittest/MEBootDiscreteIdentityTest.cpp`
现在会按 `DISCRETE_PROFILE_REPETITIONS` 输出每个 research profile 的总时间和
stage 时间。该改动只恢复观测能力，没有改变 bootstrap contract。

## 本轮 production change

原来的 cubic 计算为：

```text
3 * Delta * x^2 - 2 * x^3
```

每轮需要先生成 `x^2` 和 `x^3`，再对两个结果分别做常量乘法。现在使用等价
因式分解：

```text
x^2 * (3 * Delta - 2 * x)
```

实现位置是 `src/extension/DiscreteIntegerCleaner.cpp`。它保留了 `3 * Delta`
的精确 residue 编码，仍使用原来的 rounded scale、iteration rescale 数量和
误差账本；最终 polynomial 使用 `RelinearizeRescale`，其余 rescale 次数相应
减少一次。没有改变输出 level、scale、系数或误差门限。

在匹配的 cubic microbenchmark 中，90 个样本/候选/阶段、3 个 fresh keyset 的
汇总中位数如下：

| iteration | 原始 separate | delayed | factored | accumulated |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 6.709 ms | 6.716 ms | **6.143 ms** | 6.266 ms |
| 1 | 5.520 ms | 6.374 ms | **5.251 ms** | 5.268 ms |

factored 相对原始 separate 的两轮平均下降约 6.8%；6 个 keyset × stage 的
中位数比较中，factored 均更快。对应 oracle 误差仍满足第一轮 `< 2^-16`、
第二轮 `< 2^-33`。

## 受控完整 profiling

优化版使用两个 keyset，每个 keyset 3 次 warmup、10 次 measured calls、5 次
detailed profiles。旧库使用保留的旧 `.so` 和旧 executable，在同一工作区重新
运行一组 control case。两组都通过 correctness 检查，输出为 Discrete L20、
scale `2^40`，峰值 allocation 和 resident EVK 均为：
`14652896488` / `10497294336` bytes。

| stage | 旧库 control | 优化版 | 变化 |
| --- | ---: | ---: | ---: |
| Full GPU median | 723.076 ms | 711.962 ms | **-1.5%** |
| DigitExtract | 386.158 ms | 386.263 ms | +0.0% |
| Cleaning | 151.488 ms | 139.781 ms | **-7.7%** |
| MEBOOT low CtS | 84.488 ms | 85.913 ms | +1.7% |
| Accurate bypass and alignment | 64.744 ms | 65.009 ms | +0.4% |

原始首次 profiling 的 633.558 ms 与后续单次 control 的 723.076 ms 存在明显运行
环境漂移；因此单次先后运行不能直接确认 <2% 的全流程收益，已通过下方
ABAB 交替复测（Ticket 02）予以严格确认。

## ABAB 交替复测与 DigitExtract 拆分（Ticket 01 & 02）

按照 `docs/specs/0006-discrete-meboot-latency.md` 的测量协议，在同一会话中对三个
候选版本执行 3 轮交替复测（奇数轮 `pre_cubic -> current -> identity19`，偶数轮
`current -> pre_cubic -> identity19`；每轮每个版本均为独立进程、fresh keyset、
3 次 warmup、10 次计时调用、5 次详细 profile，共计 30 次计时调用/版本、15 次
profile/版本）。GPU 为 NVIDIA GeForce RTX 5060 Ti（Driver 610.88，P0 状态，
2745–2797 MHz graphics clock）。原始日志与汇总保存在
`docs/evidence/discrete-identity/baseline-20261019/rebaseline-summary.json`。

### 1. 端到端对比与 95% 置信区间

| 版本 | 输出 | GPU 中位数 (ms) | GPU 95% CI (ms) | GPU P95 (ms) | Wall 中位数 (ms) | Wall 95% CI (ms) |
| --- | --- | ---: | --- | ---: | ---: | --- |
| `pre_cubic`（旧库 snapshot） | L20 | 679.957 | `[679.272, 680.339]` | 688.487 | 699.893 | `[697.736, 709.833]` |
| `current`（cubic 因式分解 + 拆分计时） | L20 | **668.038** | `[667.641, 668.604]` | 669.303 | **685.735** | `[679.125, 694.718]` |
| `identity19`（同会话 MEBOOT 对照） | L21 | 267.147 | `[266.976, 274.356]` | 283.376 | 280.315 | `[277.570, 288.594]` |

- **cubic 因式分解收益确认**：`current` 相对 `pre_cubic` 的 GPU 中位数下降
  **-11.919 ms（-1.75%）**，95% 置信区间完全不重叠；三轮配对差值分别为
  `-13.074 ms`、`-12.318 ms`、`-10.508 ms`，正反顺序下均稳定更快。
- **同会话 Identity19 差距基准**：`current` 与同会话 `identity19` 的 GPU 中位数差距为
  **400.891 ms**（比值 **2.50×**；三轮配对差距 `400.211 / 387.113 / 401.546 ms`）。

### 2. 阶段中位数对比（15 次详细 profile 汇总）

| 阶段 | `pre_cubic` (ms) | `current` (ms) | 变化 |
| --- | ---: | ---: | ---: |
| StC and entry | 20.606 | 20.051 | -2.7% |
| Lift and sparse-to-dense | 4.644 | 5.272 | +13.5% |
| MEBOOT low CtS | 80.118 | 79.614 | -0.6% |
| Split and affine alignment | 4.409 | 4.404 | -0.1% |
| DigitExtract, both channels | 362.490 | 362.785 | +0.1% |
| **Cleaning, both channels** | **142.401** | **130.675** | **-8.2% (-11.726 ms)** |
| Integer recombination | 2.687 | 2.698 | +0.4% |
| Accurate Bypass | 61.276 | 61.449 | +0.3% |
| Subtract and normalize | 0.583 | 0.583 | +0.0% |

Cleaning 阶段中位数下降 **-11.726 ms（-8.23%）**，与端到端 GPU 中位数下降
（-11.919 ms）在 0.2 ms 内完全吻合。

### 3. DigitExtract 内部拆分（`current`，双通道合计 362.785 ms）

| 子阶段 | 双通道中位数 (ms) | 单通道中位数 (ms) | 占 DigitExtract 比例 | 折合单次 ct-ct 乘法 |
| --- | ---: | ---: | ---: | ---: |
| `extract/basis`（共享 Chebyshev basis，13 次乘法/通道） | 102.157 | 51.078 | 28.2% | ~3.93 ms |
| `extract/tree/0..5`（6 棵输出树，各 6 次乘法/通道） | 259.925 | 129.962 | 71.7% | ~3.61 ms（~21.8 ms/树） |
| `extract/0..5/0`（`+0.5` 偏移加法） | 0.524 | 0.262 | 0.1% | — |

单通道 6 棵输出树的中位数分别为：`21.778 / 21.859 / 21.852 / 21.854 / 21.639 / 21.965 ms`。

## 验证

已通过：

- `discrete_integer_cleaner_test`：全部 5 个现有 functional cases 通过，opt-in
  performance case 默认 skip。
- `discrete_identity_baseline_test`：通过（53 个详细 stages）。
- `bootstrap_timing_test`：3 个 cases 通过。
- `bootstrap_contract_test`：7 个 cases 通过。
- `meboot_discrete_identity_test`：62 个详细 stages 通过。
- `git diff --check`：通过。
- ABAB 交替复测（3 keysets × 3 candidates）：全部通过，L20、scale `2^40`、
  误差与 alias checks 通过，峰值 allocation `14652896488` bytes、resident EVK
  `10497294336` bytes 不变。

## 复现命令

构建相关 target：

```text
cmake --build build --target discrete_integer_cleaner_test meboot_discrete_identity_test discrete_identity_baseline_test bootstrap_timing_test bootstrap_contract_test meboot_performance_test -j4
```

局部 cubic benchmark：

```text
DISCRETE_CLEANING_PERF=1 build/unittest/discrete_integer_cleaner_test --gtest_filter=DiscreteIntegerCleaner.CubicPerformance
```

ABAB 交替复测（含同会话 Identity19）：

```text
python3 tools/research/profile_discrete_depth.py --interleaved --out docs/evidence/discrete-identity/baseline-20261019 --name rebaseline --keysets 3 --warmups 3 --repetitions 10 --profiles 5
```
