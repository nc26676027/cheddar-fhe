# Discrete rebaseline: interleaved ABAB campaign with same-session Identity19

**Spec:** `docs/specs/0006-discrete-meboot-latency.md`
**Status:** completed
**Blocked by:** 01

## 问题

现有的端到端数字是先后跑出来的，对照组只有 1 个 keyset。同一个库在两次运行之间就从 633.6 ms 变成了 723.1 ms，所以 cubic 因式分解的 −1.5% 不能算数。后面的优化也需要一个可靠的基准。

## 要做的

按父 issue 的测量规则跑一次复测，包含三个版本：

- A：e107da8 之前的旧库（`build/discrete-optimization-20261019/` 里有保留的 snapshot）
- B：当前版本（带 ① 的分开计时）
- C：Identity19 MEBOOT route

交替顺序跑，每个版本至少 3 个 fresh keyset，固定 GPU clocks / power state，记录 clocks 和驱动版本。

## 验收

- 原始日志、汇总 JSON 和环境信息放进 `docs/evidence/discrete-identity/baseline-<date>/`
- 报告每个版本的 GPU 中位数和置信区间，以及 B 的各阶段耗时（含 basis 和 6 棵树）
- 根据结果更新 `docs/research/discrete-identity/optimization-20261019.md` 里 −1.5% 的结论（确认还是撤回）
- 写下 B 和 C 的同 session 差距，作为 ③ 和 ④ 的基准
