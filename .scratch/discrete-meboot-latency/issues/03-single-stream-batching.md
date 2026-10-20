# Discrete batching: single-stream real/imag batched DigitExtract and cleaning toward < 400 ms

**Spec:** `docs/specs/0006-discrete-meboot-latency.md`
**Status:** completed
**Blocked by:** 02

## 问题

实部和虚部现在是串行跑完整的 DigitExtract 和 Integer Cleaning（`DiscreteIdentityBootstrap.cpp` 里 SplitRealImag 之后逐个处理）。slot 已经满了（32768），所以两个分量**不能**合并成一个密文，但可以在 kernel 层面一起算。

## 完成内容

1. **单密文 L2 Cache 驻留与显存峰值隔离**：
   - 维持单密文（$Z=1$）NTT/INTT 临时空间 12–16 MB，100% 驻留 RTX 5060 Ti 的 32 MB L2 Cache（>2,000 GB/s）。
   - 实虚部分别在 L29 提取 digits（各 6 个），但在 L23 汇合 12 个 digits。
   - 在 `DiscreteIntegerCleaner::EvaluateBatch` 中使用 `chunk_size = 6` 分块执行 cubic cleaning，避免 24 个含 aux 的 degree-2 密文同时驻留（节省约 1,000 MB 显存峰值）。
   - 在 `MEBOOTDiscreteIdentity::Evaluate` 中对各中间密文（`raised`, `low`, `dense`, `bypass`, `high`, `integer`）实现立即生命周期析构；在 keygen 后调用 `context->Trim()` 释放临时显存池。
2. **BasisMap 度数 13 拆分优化**：
   - 在 `src/extension/EvalPoly.cpp` 的 `BasisMap<word>::SplitBaseDegree` 中，针对奇数度 13 采用 $1 \ll \text{Log2Floor}(13) = 8$ 拆分，避免生成冗余的 $T_6$，每个通道消除 1 次平方/relinearize/rescale。
3. **实虚部分量汇合批量清理**：
   - 在 `DiscreteIdentityExecution::CleanInteger` 中无 observer 路径调用 `cleaner_->EvaluateBatch`，将 12 个 digits 批量推进 `RelinearizeBatch` 与 `RelinearizeRescaleBatch`（共享 EVK），大幅减少显存带宽开销。

## 验收结果与 Evidence

Evidence 保存在 `docs/evidence/discrete-identity/single-stream-batching-20261019/single-stream-batching-summary.json`（3 keysets × 3 candidates，交替 ABAB 复测）。

- **硬约束全部满足**：
  - 输出 Level: **L20**
  - 输出 Scale: **$2^{40}$**
  - 最大解密误差: worst bits = **18.844**（$< 2^{-18} \approx 3.81 \times 10^{-6}$）
  - 整型清理误差: **$1.77 \times 10^{-11}$**（$< 2^{-33} \approx 1.16 \times 10^{-10}$）
  - 显存峰值: **`15,364,354,856` 字节**（严格位于硬上限 `15,385,541,312` 字节以内）
  - Resident EVK: **`10,497,294,336` 字节**（严格位于上限 `10,497,294,336` 字节以内）
  - 全程单 stream 执行，无并发数据竞态。
- **性能与收益**：
  - `pre_cubic` GPU 中位数: **661.472 ms**（95% CI: `[657.070, 664.417]` ms）
  - `current` GPU 中位数: **567.095 ms**（95% CI: `[564.678, 570.044]` ms）
  - 相对 `pre_cubic` 降幅: **-94.377 ms（-14.27%）**（每轮配对差值 `-93.069 / -94.493 / -93.513` ms，均高度稳定）
  - 相对原始基线（668.038 ms）总降幅: **-100.943 ms（-15.11%）**
  - DigitExtract 阶段耗时: 350.95 ms $\to$ **286.36 ms**（-64.59 ms / -18.4%）
  - Cleaning 阶段耗时: 137.39 ms $\to$ **107.81 ms**（-29.58 ms / -21.5%）
  - 距离里程碑目标（< 400 ms）剩余差距: **167.10 ms**
  - 距离同会话 Identity19 对照（253.95 - 256.34 ms）差距从 400.89 ms (2.50×) 缩减至 **312.22 ms (2.23×)**
- **测试全部常绿**：
  1. `centered_crt_test`: PASSED (6/6)
  2. `meboot_discrete_identity_test`: PASSED (1/1)
  3. `discrete_integer_cleaner_test`: PASSED (5/5)
  4. `discrete_identity_baseline_test`: PASSED (1/1)
  5. `bootstrap_timing_test`: PASSED (3/3)
  6. `bootstrap_contract_test`: PASSED (7/7)

