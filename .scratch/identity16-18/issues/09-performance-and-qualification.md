# 09: Qualify native performance preservation and existing-route regressions

**What to build:** A reproducible evidence package demonstrates whether exact
selected joint/ordinary refreshes preserve native performance and whether
existing Cheddar routes remain unaffected.

**Blocked by:** 08: Run exact selected refreshes within the native GPU memory budget.

**Status:** execution-in-progress (owner authorized 2026-10-13)

- [ ] All selected correctness, level/scale, LCR eligibility and peak-memory
  gates pass before collecting acceptance timings.
- [ ] Five warmups and at least30 calls per route in each of three independent
  paired runs, alternating route/control order and serial GPU execution.
- [ ] Report host and CUDA-event median/p95, paired95% confidence intervals,
  asynchronous stage intervals, precision, depth and measured peak-live memory.
- [ ] Optimized versus same-work native-control median ratio<=1.00 and upper
  confidence bound<=1.03 on both clocks; apply the same gate to retained
  old-route after/before runs. Inconclusive runs remain unqualified.
- [ ] Selected joint meets the selected ordinary latency target on the common
  identity contract, or the report explicitly marks a performance blocker.
  Never omit bypass, lower precision/depth or change chains to pass.
- [ ] Any extra fusion targets a measured hot path and is verified against
  its corresponding native control; fused prefix/LCR, double hoisting and
  native product/ModDown epilogues remain enabled.
- [ ] Record actual fused-versus-generic dispatch for selected shapes, not just
  global flags; measure and resolve any performance-costly fallback.
- [ ] Run retained32/64-bit bootstrap and basic RNS/NTT/rescale/KS regressions,
  retaining source/profile/fixture hashes and raw results.
- [ ] Document exact reproduction commands, remaining limitations and
  qualification status without converting CPU reference times into GPU claims.

## Execution evidence (2026-10-13)

Gated. No selected acceptance timings collected because correctness/range/memory qualification is incomplete. No performance preservation claim.

Acceptance checkboxes remain open until their complete evidence exists. See ../execution/status.md for cross-ticket qualification.

## Continuation evidence

All retained regression targets rebuilt in WSL CUDA Release. The serial
queue completed on 2026-10-14: eleven targets, 55 passing test instances,
two existing configured skips and no failures. The skipped MEBoot 30/35-bit
bootstrap cases are only configured for the 40-bit profile. Both classic
and configured MEBoot bootstrap pass their 32/64-bit profiles.

BasicTest ran nine focused HMult/HRot/AutonomousModSwitchOperations
instances with zero warmups for correctness only. Earlier interrupted
broader/warmed runs remain preserved. This is not a completed full
BasicTest suite or performance evidence.

The final selected coefficient-rounding and pre-preparation encoder-policy
tests both pass. Raw results are in
`../execution/native/retained-after/`, summarized by `results-summary.json`,
and `../execution/native/encoder-policy-final.log`.
Final source/binary hashes and reproduction commands are in
`../execution/native/source-final.sha256`,
`../execution/native/binaries-final.sha256` and
`../execution/native/reproduce-final.md`.

No formal performance data collected. Joint standalone endpoints fail
on all three GPU key sets; the pinned encrypted Go reference and a
plaintext-only StC oracle independently reproduce the failure.
All performance checkboxes and the full dispatch/ownership audit remain
open. No performance-preservation claim.
