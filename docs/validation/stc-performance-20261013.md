# StC performance validation, 2026-10-13

Final cleaned implementation: verified lossless encoded-plaintext compaction,
register-cached GS accumulation and kernel-argument pointer tables.
Default StC folding remains [5,5,5], BS8; keys, levels and scales are unchanged.
RTX 5060 Ti, CUDA 12.8, Release build, N=65536, 32768 slots, scale 2^40.
Evidence: `.scratch/meboot-performance/stc-tuning/final/`.
The interrupted October 12 run was resumed on October 13; incomplete Bootstrap
evidence was replaced by a complete run. GPU tests ran serially.

## Cause and fix

StC is lower in the modulus chain but still uses 23/22/21 Q primes plus 12 P
primes (32-bit), or 15/14/13 Q plus 7 P (64-bit). Active decomposition beta
is 3/2/2. Its three stages contain 63/63/32 diagonals, 158 in total.
MEBOOT CtS contains 93 diagonals and specialized prefix/LCR operators.
RNS count alone therefore does not predict comparable transform cost.

Diagnostic phase events identified BabyStep and GS accumulation as roughly
two-thirds of StC time. Wider BS and a parallel GS rewrite were investigated;
the parallel rewrite was slower, and default BS8 was retained.
Periodic diagonal plaintexts contain repeated encoded NTT blocks. Preparation
checks every RNS word before substituting a compact table. Any mismatch
falls back to the full encoding. Small GS shapes cache baby values in
registers and pass pointer tables as kernel arguments.
Final cuobjdump reports BS8 stack/local storage 0, registers 40/56
for 32/64-bit kernels. This is static resource evidence, not occupancy
or measured bandwidth. Temporary diagnostic kernels/logging were removed.

## Fixed-input StC comparison

Five warmups, 30 rotating-order samples per configuration; same input and
shared keys. Config 0: legacy BS8; 1: optimized BS8; 2: exploratory BS16.

| Word | Config | Host median ms | CUDA median ms | CUDA change |
|---|---|---:|---:|---:|
| 32 | 0 | 25.91050 | 23.79960 | +0.00% |
| 32 | 1 | 24.22475 | 21.91535 | -7.92% |
| 32 | 2 | 25.23275 | 22.68990 | -4.66% |
| 64 | 0 | 34.63061 | 32.89262 | +0.00% |
| 64 | 1 | 32.83521 | 30.70664 | -6.65% |
| 64 | 2 | 33.09670 | 30.52405 | -7.20% |

Optimized BS8 matches ALL Bx/Ax residues of legacy BS8 for both words.
Output modulus basis, scale and slot metadata match. BS16 is checked
numerically at 1e-4 and is not the production default.
The first two stages compact all 126 diagonals. Compact storage is
18,111,744 bytes (17.27 MiB) / 22,385,664 bytes (21.35 MiB).
Full encodings remain allocated for MinKS/general fallback: this is an
additional cache, not a device memory reduction. No memory peak was measured.

## Complete unprofiled Bootstrap comparison

Same-run Basic versus MEBOOT, five warmups and 30 alternating samples.
Both precision alarms and Basic latency targets passed.

| Word | Basic host ms | MEBOOT host ms | Change | Basic SNR dB | MEBOOT SNR dB |
|---|---:|---:|---:|---:|---:|
| 32 | 83.69415 | 70.54865 | -15.71% | 128.86652 | 128.92669 |
| 64 | 111.18809 | 101.26063 | -8.93% | 117.33225 | 117.37924 |

The one-level CtS saving remains asserted by the paired tests.
Used-key payloads remain 4,346,347,520 / 5,231,869,952 bytes (32-bit Basic/MEBOOT)
and 4,758,437,888 / 5,798,625,280 bytes (64-bit). These describe provisioned
keys in the shared store, not standalone minimum key sets.

## Final complete Bootstrap profiling

Separate profiling run, 30 samples per word/path (120 total).
Profiled and ordinary outputs match all residues and metadata.
Ordered CUDA intervals cover the actual BootImpl without per-stage sync.
The following are mean event intervals; stage shares sum to the total.

| Stage | 32 Basic ms | 32 MEBOOT ms | 64 Basic ms | 64 MEBOOT ms |
|---|---:|---:|---:|---:|
| ScaleUp | 0.07207 | 0.07059 | 0.07417 | 0.08000 |
| ModUp | 2.77746 | 2.01247 | 2.57156 | 0.61466 |
| Trace | 0.00114 | 0.00162 | 0.00234 | 0.00058 |
| CtS | 35.09334 | 21.45945 | 47.14284 | 30.47036 |
| SplitRealImag | 1.48947 | 1.49461 | 2.13245 | 2.21846 |
| EvalModReal | 13.28199 | 13.33052 | 18.93364 | 20.28532 |
| EvalModImag | 13.33288 | 13.33942 | 18.92370 | 20.27913 |
| Recombine | 0.06566 | 0.06279 | 0.08469 | 0.09503 |
| StC | 18.93956 | 18.89990 | 24.32712 | 27.67130 |
| Finalize | 0.01083 | 0.00878 | 0.00064 | 0.00059 |
| ReleaseTemps | 0.00104 | 0.00108 | 0.00083 | 0.00082 |
| Total | 85.06544 | 70.68122 | 114.19399 | 101.71626 |

| Word/path | Ordinary host median ms | Profile host median ms | Event change |
|---|---:|---:|---:|
| 32/standard | 90.97560 | 83.45350 | +0.06% |
| 32/meboot | 76.64800 | 70.93655 | +0.75% |
| 64/standard | 109.72801 | 111.25726 | +0.40% |
| 64/meboot | 99.48100 | 100.43605 | +0.19% |

Maximum stage-accounting residual: 0.000160 ms.
CUDA events can include submission gaps. This run also exhibits event/host
clock discrepancies; the two clocks are reported independently and their
difference is not interpreted as CPU overhead. Historical runs used
different timing conditions; the fixed-input legacy/optimized StC comparison
establishes this patch's speedup, not subtraction of old Bootstrap medians.

## Validation

| XML | Tests | Failures | Skips |
|---|---:|---:|---:|
| aks_key_test.xml | 4 | 0 | 0 |
| boot_test.xml | 8 | 0 | 2 |
| lcr_step_test.xml | 5 | 0 | 0 |
| linear_transform_test.xml | 3 | 0 | 0 |
| meboot_cts_test.xml | 10 | 0 | 0 |
| meboot_matrix_planner_test.xml | 3 | 0 | 0 |
| meboot_performance_test.xml | 2 | 0 | 0 |
| tuning.xml | 2 | 0 | 0 |

35 unique cases passed, zero failed, two existing expected skips. Profiling
reruns the two paired cases and is not counted as two additional unique cases.
Source hashes match for library code, tests and parameters; only the
reproduction README changed during validation. Helpers compile and
`git diff --check` passes. No commit, push, merge or Phase 2 work.

Reproduction: `run_meboot_quality.sh`, `stc_tuning_test`,
`summarize_stc_tuning.py`, then `run_bootstrap_profile.sh` (serial GPU work).
See `unittest/tools/README.md` and final logs/XML/JSON for exact evidence.
