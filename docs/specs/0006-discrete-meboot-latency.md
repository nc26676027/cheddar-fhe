# Spec: Discrete MEBOOT Latency

**Status:** ready-for-agent
**Approved:** 2026-10-19；用户在 grilling 中确认了目标、硬约束、测量规则、ticket 拆分和测试接入点。
**Date:** 2026-10-19
**Source revision:** e107da8
**Delivery:** 仓库关闭了 GitHub Issues，按本地 tracker 规定发布。
**Tickets:** `.scratch/discrete-meboot-latency/issues/`

## Problem Statement

Discrete MEBOOT route 的整数提取更精确，代价是延迟太高。同环境下 GPU 中位数约
712 ms，而 Identity19 MEBOOT route 约 244 ms。两条路线共用的阶段（entry、lift、
low CtS、Accurate Bypass）耗时基本一样，约 155 ms，差距全部来自 Discrete 独有的
DigitExtract 和 Integer Cleaning：约 478–540 ms，而 Identity19 的 EvalMod 只要
91.5 ms。

维护者现在有三个问题：

- **看不清瓶颈。** DigitExtract 的共享 basis 和六棵输出树算在同一个 profile
  stage 里，没法判断该先优化哪里。
- **性能结论不可靠。** 新旧版本是先后各跑一组测出来的，运行环境之间的漂移
  （+14%）比要证明的收益（−1.5%）大一个数量级。
- **现有手段不够。** 现在的表示方式是每个分量 6 个 1-bit digit，靠调参数很难
  超过约 2 倍的提速，而要低于 Identity19 需要约 4.5 倍。

## Solution

分两档降低 Discrete MEBOOT route 的 GPU 中位数：

1. **里程碑**：< 400 ms
2. **最终目标**：低于同一次运行里测的 Identity19 MEBOOT route

做法分四步：

1. 先补上观测能力，看清 DigitExtract 里 basis 和各输出树的耗时。
2. 用交替测量的规则重新测一次基线。
3. 两个分量（实部和虚部）合并执行加调参数，冲 400 ms。
4. 同时用 CPU 原型评估新的 digit 表示方式，判断能不能达到 Identity19 这一档。

全程不放宽输出 level、scale 和误差门限，显存只允许小幅增加。

## User Stories

1. 作为 Discrete route 维护者，我希望 profile 能分别显示 DigitExtract 共享 basis 的耗时，这样我知道 basis 占了多少。
2. 作为 Discrete route 维护者，我希望 profile 能分别显示六棵输出树各自的耗时，这样我能找到最贵的树。
3. 作为 Discrete route 维护者，我希望 basis 和六棵树的时间加起来与整体提取时间吻合，这样我相信拆开后的数据没有漏算。
4. 作为 Discrete route 维护者，我希望 profile 里的 stage key 叫 `accurate-bypass` 而不是 `high-cts`，这样 profile 和 CONTEXT.md 用的是同一套词。
5. 作为研究报告作者，我希望汇总脚本同时认新旧两个 stage key，这样旧的 evidence 日志在改名后还能汇总。
6. 作为研究报告作者，我希望汇总脚本把这个阶段显示为 "Accurate Bypass"，这样报告和 glossary 一致。
7. 作为 Discrete route 维护者，我希望观测改动不改变任何输出、level、scale 或误差，这样加观测不会带来回归风险。
8. 作为性能评审者，我希望新旧版本按 ABAB 交替运行，这样环境漂移对两边影响一样。
9. 作为性能评审者，我希望 Identity19 和 Discrete 在同一次运行里测量，这样 "低于 Identity19" 有可比的对照。
10. 作为性能评审者，我希望每个版本至少用 3 个 fresh keyset，这样结论不依赖某一组 key。
11. 作为性能评审者，我希望测量时固定 GPU clocks 和 power state，并记录驱动和时钟信息，这样结果能复现。
12. 作为性能评审者，我希望报告给出 GPU 中位数和置信区间，这样能判断小于 2% 的变化是不是真的。
13. 作为性能评审者，我希望原始日志、汇总 JSON 和环境信息都存进 evidence 目录，这样结论可以追溯。
14. 作为研究报告作者，我希望复测后能确认或撤回 cubic 因式分解的 −1.5% 结论，这样文档不会留下没证实的收益。
15. 作为 Discrete route 维护者，我希望复测结果包含 basis 和各输出树的分阶段耗时，作为后续优化的基准。
16. 作为 Discrete route 维护者，我希望实部和虚部的 DigitExtract 在同一组 kernel 调用里完成，这样减少重复的启动和访存开销。
17. 作为 Discrete route 维护者，我希望两个分量的 Integer Cleaning（共 24 次 cubic）也合并执行，这样 cleaning 阶段同样受益。
18. 作为 Discrete route 维护者，我希望合并执行只用单个 stream，这样在多 stream 通过串行正确性检查之前不引入并发风险。
19. 作为 Discrete route 维护者，我希望能在硬约束内调整 baby step、extraction scale 和 cleaning 的 rescale 安排，并以分阶段数据为依据。
20. 作为 Discrete route 维护者，我希望已经被否掉的方案（d95/L19、LP-fit 和 Remez d47、basis scale 2^58、低 level 的 latency 路线）不再重复评估，这样不浪费时间。
21. 作为下游应用开发者，我希望 Discrete MEBOOT route 的输出仍是 L20、scale 2^40，这样我的电路 level 预算不用改。
22. 作为下游应用开发者，我希望最终最大复数误差仍 < 2^-18，这样应用精度不受影响。
23. 作为下游应用开发者，我希望 clean_integer_error 仍 < 2^-33，这样整数提取仍然精确。
24. 作为部署者，我希望峰值 allocation 和 resident EVK 都不超过现值的 +5%，这样在 16 GiB 显卡上还能跑。
25. 作为下游应用开发者，我希望 `BootContext` 的 route 选择和调用方式不变，这样优化对调用方透明。
26. 作为算法研究者，我希望有 3 个 2-bit digit 方案的 CPU 原型，给出在 2^-33 门槛下需要的乘法次数和 level。
27. 作为算法研究者，我希望有 "直接提取整数 + 能收敛到多个整数的清理多项式" 方案的 CPU 原型，给出同样的指标。
28. 作为算法研究者，我希望超过 8 个 level 的候选直接否掉，这样不违反零空余的 level 预算。
29. 作为算法研究者，我希望原型按复测得到的单次乘法耗时估算 GPU 延迟，并和现在的 6×1-bit 方案对比。
30. 作为算法研究者，我希望每个原型输出 certificate 风格的 JSON（乘法次数、level、误差上界），这样结论可复核。
31. 作为技术负责人，我希望每个原型都给出 "继续做" 或 "放弃" 的结论和理由，这样能决定要不要开实现 ticket。
32. 作为技术负责人，我希望如果决定换 digit 表示方式，另外评估是否写 ADR，这样不可逆的决定有记录。
33. 作为技术负责人，我希望 400 ms 和 "低于 Identity19" 两档都用同一套测量规则验收，这样标准一致。
34. 作为执行 ticket 的 agent，我希望每个 ticket 写清依赖关系（01 → 02 → 03，04 可以和 01、02 同时做），这样我知道从哪里开始。

## Implementation Decisions

### 目标与验收

- 只比较 GPU 中位数。两档目标都以同一次运行里测的对照为准，历史数字只作参考。
- 硬约束：输出 L20、scale 2^40；最终最大复数误差 < 2^-18；clean_integer_error < 2^-33；
  峰值 allocation 和 resident EVK 都 ≤ 现值 +5%（14,652,896,488 / 10,497,294,336 bytes）。
- 和 Identity19 共用的阶段不在本 spec 范围内，因为优化它们不会缩小两条路线的相对差距。

### 观测（ticket 01）

- DigitExtract 的批量多项式求值把共享 basis 的构造和每棵输出树作为独立的
  research profile stage 发出，名字是 `extract/basis` 和 `extract/tree/<k>`，
  放在现有的 per-component 前缀下。
- `high-cts` 统一改名为 `accurate-bypass`，包括路由代码、测试里的 stage 匹配和研究工具。
- production 的 `BootProfile` 粒度不变。研究粒度和 production 粒度分开，沿用 spec 0005 的决定。
- 汇总工具同时认 `high-cts` 和 `accurate-bypass`，显示名都映射成 "Accurate Bypass"。

### 测量规则（ticket 02，所有性能结论都适用）

- 测量工具扩展为按 ABAB 交替运行多个 binary：旧库 snapshot、当前版本、Identity19。
- 每个版本至少 3 个 fresh keyset；固定 GPU clocks / power state；记录驱动、时钟和 GPU 型号。
- 输出每个版本的 GPU 中位数、置信区间和分阶段中位数。

### 两个分量合并执行（ticket 03）

- 实部和虚部**不能**合并成一个密文：slot 已满（32768），N 个实数系数只对应 N/2 个复数 slot。
- 改为一次处理多个密文：DigitExtract（共享 basis + 六棵输出树）和 Integer Cleaning
  的 cubic 对两个分量合并成同一组 kernel 调用。只用单个 stream。
- 合并执行是 Discrete route 内部的实现，对外的 `BootContext` / route 接口不变。
- 调参数在硬约束内进行。level 预算没有空余（L29 → 6 个提取 level → 2 个 cleaning
  level → L21 → 最终 L20），任何需要额外 level 的参数都不行。
- 合并执行会同时持有两个分量的临时数据，显存上限（+5%，测试机 16 GiB）可能比速度更先成为约束，必须在 evidence 里报告峰值。

### digit 表示方式原型（ticket 04）

- 只在 CPU 上做（明文或模拟误差），不接 GPU 路径。
- 候选 (a)：3 个 2-bit digit，cleaning 多项式收敛到 {0,1,2,3}。
- 候选 (b)：一个多项式直接提取 I ∈ [-22, 21]，再用对多个整数都能收敛的多项式清理。
- 评估指标：在 2^-33 整数残差门槛下（且最终误差 < 2^-18）需要的密文乘法次数和
  level；能否放进 6 + 2 个 level（超过 8 个 level 直接否掉）；按 02 的单次乘法耗时估算 GPU 延迟。
- 结论写进 discrete-identity 研究目录；决定换表示方式时，另外评估是否写 ADR。

## Testing Decisions

- **好测试的标准**：只验证外部可见的行为，包括输出 level、scale、误差门限、alias
  检查、显存，以及 profile 里出现的 stage 和它们的时间是否吻合。不验证 kernel
  的组织方式、调用顺序或中间密文的布局。
- **主接入点**：Discrete MEBOOT route 的端到端加密测试，从 `BootContext` 选
  `BootRoute::kDiscreteMEBoot`。所有硬约束都在这一层验证。
  - 01：检查 `extract/basis`、`extract/tree/0..5`、`accurate-bypass` 这些 stage 都出现了，basis 和各树的时间加起来与整体提取时间吻合（误差在 profile 开销以内），输出和改动前一致。
  - 03：合并执行后，所有门槛和显存上限照样满足。**不单独测 batched API。**
- **回归**：Integer Cleaning 现有的 functional cases（每轮误差 < 2^-16 / < 2^-33）、
  Discrete identity baseline 测试、bootstrap timing 生命周期测试都必须继续通过。
- **工具接入点**：测量和汇总工具用已有的 evidence 日志回放来验证，结果必须不变（包括旧的 `high-cts` key）。
- **原型接入点**：原型自己生成 certificate JSON，可复核；不加 GPU 测试。
- **参考实现**：restricted-domain 和 Lattigo Remez 研究的 certificate JSON 与
  all-cells 检查；现有 discrete depth campaign 的 profile 和汇总流程；spec 0005 的分层 profiling 测试。
- 性能数字**不**写进 CI 门槛。性能结论只看按测量规则产生的 evidence。

## Out of Scope

- 优化和 Identity19 共用的阶段：entry、lift、low CtS、Accurate Bypass。
- 多 stream 并行执行（要先通过串行正确性检查）。
- 放宽输出 level、scale 或误差门限，包括 d95/L19 和低 level 的 latency 路线。
- 把实部和虚部合并成一个密文（slot 已满，做不到）。
- 正式实现新的 digit 表示方式。04 只做原型和决策，实现另开 ticket。
- 改变 production `BootProfile` 的 stage 粒度。
- 迁移到 GitHub Issues（要等仓库打开 Issues 并授权）。

## Further Notes

- 基准数字（e107da8）：Discrete 约 712 ms（同环境旧库对照 723 ms）；DigitExtract
  约 386 ms；Integer Cleaning 约 140 ms；Identity19 历史值 243.6 ms GPU。
- 精度余量：最终误差约 0.9 bit，整数残差约 2.8 bit。能拿来换速度的余量主要在整数残差上。
- 预算：< 400 ms 要求 Discrete 独有部分快约 1.95 倍；低于 Identity19 要求快约 4.5 倍。
- 术语见 CONTEXT.md：Discrete MEBOOT route、Accurate Bypass、DigitExtract、Integer Cleaning。
  相关 ADR：0009（Discrete EvalRound extension seam）、0010（Discrete MEBOOT production route）。
- 背景：discrete-identity 研究目录下的 optimization-20261019 记录。

## Tickets

| Ticket | 内容 | 依赖 |
| --- | --- | --- |
| 01 | DigitExtract 分开计时 + `high-cts` 改名为 `accurate-bypass` | 无 |
| 02 | ABAB 交替复测，同一次运行里测 Identity19 | 01 |
| 03 | 两个分量合并执行（单 stream）+ 调参，冲 400 ms | 02 |
| 04 | 两种新 digit 表示方式的 CPU 原型 | 无（耗时估算引用 02 的结果） |
