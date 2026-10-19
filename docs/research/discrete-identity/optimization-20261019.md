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

原始首次 profiling 的 633.558 ms 与后续 control 的 723.076 ms 存在明显运行
环境漂移；因此最终端到端结论使用同条件的旧库 control，而不是把首次运行和
优化运行直接相减。完整 profiling 支持约 1.5% 的端到端收益，局部 cleaning
收益更稳定，约 7.7%。后续应在固定 GPU clocks / power state 的重复 campaign
上继续确认小于 2% 的全流程收益。

## 验证

已通过：

- `discrete_integer_cleaner_test`：全部 5 个现有 functional cases 通过，opt-in
  performance case 默认 skip。
- `discrete_identity_baseline_test`：通过。
- `bootstrap_timing_test`：3 个 cases 通过。
- `git diff --check`：通过。
- 两个优化 full-profile keyset 和一个旧库 control keyset：全部通过，
  L20、scale `2^40`、误差与 alias checks 通过。

性能复现实验的原始日志和 JSON 汇总保存在
`docs/evidence/discrete-identity/optimization-20261019/`。构建目录中的
`build/discrete-optimization-20261019/` 保留了旧库 snapshot、microbenchmark
运行脚本和完整日志。

## 复现命令

构建相关 target：

```text
cmake --build build --target discrete_integer_cleaner_test meboot_discrete_identity_test discrete_identity_baseline_test bootstrap_timing_test -j4
```

局部 cubic benchmark：

```text
DISCRETE_CLEANING_PERF=1 build/unittest/discrete_integer_cleaner_test --gtest_filter=DiscreteIntegerCleaner.CubicPerformance
```

完整 profiling：

```text
python3 tools/research/profile_discrete_depth.py --out build/discrete-optimization-20261019 --name optimized --keysets 2 --repetitions 10 --warmups 3 --profiles 5
```
