# Identity16-18 review fixes and GPU performance — 2026-10-14

Response to the two-axis code review of the selected Identity16-18 port.
No commit, push or repository setting change was made. q0, the full Q/P and
entry chains, the 18 application levels and the level20/scale2^40 output are
unchanged.

## Findings and resolution

| Finding | Severity | Resolution |
| --- | --- | --- |
| Scaling binding re-expressed as scattered literals; metadata relabels could mask a wrong physical gain | P1 | `Identity16Profile::ValidateScalingBinding()` now asserts the lift, CtS/return, and correction gain identities once at construction. `Identity16Execution` asserts the real post-lift, post-correction, post-Mod1 and final scales at their arithmetic boundaries. |
| Required selected-route performance qualification was missing | P1 | `Identity16PerformanceTest.cpp` plus `run_identity16_performance.sh` add the public-seam benchmark: 5 warmups, 30 calls per route per run, 3 alternating runs, host and CUDA-event median/p95 and paired deltas. |
| Legacy/matched-work control absent | P1 | The retained `bootparam_40_64bit_lowp_entry.json` paired MEBOOT-vs-standard run is recorded as the legacy control beside the selected comparison. |
| Two-limb CRT qualification wording implied a proof | P2 | `identity16_lcr_bound.py` is relabelled as a runtime envelope; native preparation verifies the encoded diagonal norms and the shared GPU CRT boundary. Security proof stays explicitly out of scope. |
| Parameter `implementation_status` was stale | P2 | Both selected parameter files now read `precision-qualified; selected-performance-qualified; security-unqualified`. |
| Forbidden glossary synonym `C2S` in a comment | Hard (Standards) | Renamed to the CONTEXT.md term `CtS bypass`. |

## Corrected reasoning

An earlier candidate assertion compared the entry boundary against the ideal
`2^33` metadata. The physical lift is real (`entry_scale * 32768 = 2^48`), but
the encoder's IFFT inverts StC before coefficient encoding, so decoded entry
slots must not be compared against an ideal relabel. The boundary assertion
was removed and the identity is instead carried by
`ValidateScalingBinding`. This is the same representation mistake recorded in
the coupled binding report.

## Selected GPU performance (RTX 5060 Ti, 16311 MiB, WSL, Release)

90 samples per route across three independent fresh-key runs; joint and
ordinary alternate order. Both return 32768 slots, level 20, scale 2^40.

| Route | Host median | Host p95 | CUDA median | CUDA p95 | Peak pool |
| --- | ---: | ---: | ---: | ---: | ---: |
| joint4434 | 265.659 ms | 285.539 ms | 271.234 ms | 277.330 ms | 14.25 GB |
| ordinary | 272.881 ms | 290.561 ms | 271.502 ms | 280.177 ms | 13.17 GB |

Paired joint-minus-ordinary: host `-5.493 ms` (95% CI `[-8.695, -2.291]`),
CUDA `-0.756 ms` (95% CI `[-2.016, 0.504]`). Median ratios 0.9735 host /
0.9990 CUDA, so the spec's `joint <= ordinary` integration target passes.

## Legacy matched-work control (single run, 3 repeats)

`Experiment/LowQ0PPerformance64` with `bootparam_40_64bit_lowp_entry.json`:
standard median host 101.31 ms, MEBOOT median host 99.0-100.6 ms; standard
max error 1.43e-6 (SNR 124.97 dB), MEBOOT 1.26e-6 (125.09 dB). This control
compares the retained legacy route at its own depth policy and is not an
equal-depth A/B against the selected profile.

## Verification

- `cmake --build build -j 4`: passed.
- `git diff --check`: passed.
- `Identity16Selected.ExactContracts` and `NativeEncryptedMod1`: passed.
- `Identity16Selected.JointRefreshDiagnostic` (`constant-mixed`): passed at
  max complex error 3.32738e-6 with exact public/stage residues and
  level20/scale2^40 output, under the new boundary assertions.
- Full `identity16_test`: 7 passed, 1 skipped (`IndependentMatrixOracle`
  without its optional oracle environment), including both selected routes,
  all four refresh patterns, and the eight-term prefix residue test.

Remaining open item: an equal-depth, equal-precision A/B against the legacy
Cheddar route. Security screening remains deferred.

## Post-refactor profiling, 2026-10-14

After moving the real/imaginary split into `Context::SplitRealImag` and reusing
it from `BootContext`, `MEBootCtS` and `Identity16Execution`, the same paired
harness was rerun (3 alternating fresh-key runs, 5 warmups, 30 steady-state
calls, per-stage profiling at 3 repeats). Output stays 32768 slots, level 20,
scale 2^40 on both routes; peak pool memory is unchanged (13.27 GiB joint /
12.27 GiB ordinary).

| Route | Host median | Host p95 | CUDA median | CUDA p95 |
| --- | ---: | ---: | ---: | ---: |
| joint4434 | 250.363 ms | 273.940 ms | 258.639 ms | 265.561 ms |
| ordinary | 258.583 ms | 276.651 ms | 260.036 ms | 264.666 ms |

Paired joint-minus-ordinary: host `-6.985 ms` (95% CI `[-10.614, -3.357]`),
CUDA `-1.643 ms` (95% CI `[-2.843, -0.443]`). Both intervals now exclude zero,
so the `joint <= ordinary` target passes on both clocks, not just on medians.

### Median per-stage GPU time

| Stage | joint4434 | ordinary |
| --- | ---: | ---: |
| StC | 19.500 ms | 18.263 ms |
| ScaleDown | 0.008 ms | 0.007 ms |
| Entry | 0.775 ms | 0.231 ms |
| ModUpAndBypass | 4.590 ms | 4.364 ms |
| CtS | 73.639 ms | 103.010 ms |
| Split | 3.478 ms | 3.358 ms |
| Mod1AndCorrectionReal | 49.565 ms | 36.706 ms |
| Mod1AndCorrectionImag | 50.482 ms | 35.380 ms |
| BypassAndFinalCombine | 59.234 ms | 55.054 ms |
| ReleaseTemps | 0.001 ms | 0.001 ms |

The MEBOOT CtS advantage is the expected one: 73.6 ms versus 103.0 ms. Joint
pays it back in the higher-degree Mod1 (degree 62 versus 30, two channels), so
the net win is a few percent rather than a large multiple. The bypass stage is
the largest single remaining cost on both routes and is the clearest next
optimization target.
