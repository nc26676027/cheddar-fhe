# Discrete profiling: split DigitExtract basis/output trees and rename high-cts

**Spec:** `docs/specs/0006-discrete-meboot-latency.md`
**Status:** completed
**Blocked by:** 无

## 问题

profile 里的 `cleaner-input-*/extract/0/0`（约 192 ms/分量）把共享 basis 和六棵输出树全算在一起，`extract/1..5/0` 只有约 0.04 ms 的加法。所以现在看不出 basis 和各输出树分别花了多少，也就没法判断 DigitExtract 该从哪里下手。

另外，代码里的 stage key `high-cts` 和 `CONTEXT.md` 里的正式名字 **Accurate Bypass** 对不上。

## 要做的

1. 在 `EvalPolyBatch` 的 DigitExtract 路径上，把共享 basis 的构造和每棵输出树分开成独立的 profile stage（例如 `.../extract/basis`、`.../extract/tree/<k>`）。
2. 把 stage key `high-cts` 改名为 `accurate-bypass`，涉及 `src/extension/DiscreteIdentityBootstrap.cpp`、`unittest/DiscreteIdentityBaselineTest.cpp`、`unittest/MEBootDiscreteIdentityTest.cpp`。
3. `tools/research/summarize_discrete_depth_campaign.py` 同时认新旧两个 key，显示名改成 "Accurate Bypass"，保证已有的 evidence 日志还能正常汇总。

## 验收

- 只改观测：bootstrap contract、输出 level 和 scale、系数、误差都不变
- `discrete_integer_cleaner_test`、`discrete_identity_baseline_test`、`meboot_discrete_identity_test`、`bootstrap_timing_test` 全部通过
- 一次 profile 输出里能看到 basis 和 6 棵树各自的 GPU 时间，而且加起来和原来的 `extract/0/0` 吻合（误差在 profile 开销以内）
- 用旧日志跑汇总脚本，结果不变
