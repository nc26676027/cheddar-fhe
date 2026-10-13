# Complete Bootstrap phase profiling — 2026-10-12

MEBOOT's reduction comes primarily from CtS and secondarily from ModUp. The
remaining MEBOOT GPU time is concentrated in both EvalMod calls (36–39%),
CtS (30–31%) and StC (~28%). This report profiles the real BootImpl call chain,
not a reimplementation or the sum of separately synchronized stages.

## Measurement method and validation

Hardware/software: RTX 5060 Ti, WSL Ubuntu, CUDA 12.8.93, Release. N=65536,
32768 complex slots, scale 2^40, accepted subring-secret input contract,
MEBOOT folding [4,4,3,4]. Each word type shares its input ciphertext and
main/subring secrets and evaluation-key store between Basic and MEBOOT.

Five warmups precede 30 timed evaluations per path, for both ordinary and
profiled Boot. Basic/MEBOOT ordering alternates within each mode. Ordinary
timing precedes profiling, so measured timing changes also include run-order
and clock/workload variation. Preparation, encryption and key generation
are excluded.

ProfileBoot pre-creates events outside the timed region, then records ordered
events on the default stream at actual BootImpl phase boundaries. No
intermediate device synchronization is added. All events are resolved only
after the complete operation. GPU event intervals include device execution
and possible host submission gaps; they are not exclusive kernel time.
Host submission intervals can include implicit CUDA stalls and are not
exclusive CPU execution time. Nsight Systems/Compute CLI was unavailable;
kernel occupancy, bandwidth and individual primitive costs were not measured.

Both paired tests passed (32/64-bit). All four profiled outputs matched their
ordinary outputs exactly in both ciphertext components and in level/scale/slot
metadata. All 120 measured profiles passed stage-total accounting; the maximum
CSV accounting residual was 0.000338 ms. Both paired Basic latency and
precision alarms passed. Prior component regressions are retained separately;
this observational change did not rerun the earlier 33-case suite.

## Ordinary latency and profiling timing change

These are the medians from this run, not the earlier M1 acceptance run.

| Word/path | Ordinary host ms | Profiled host ms | Change | Ordinary GPU ms | Profiled GPU ms | Change |
|---|---:|---:|---:|---:|---:|---:|
| 32 Basic | 101.635 | 102.548 | +0.90% | 94.651 | 95.869 | +1.29% |
| 32 MEBOOT | 86.287 | 88.250 | +2.28% | 78.689 | 80.166 | +1.88% |
| 64 Basic | 133.310 | 134.022 | +0.53% | 127.022 | 126.570 | -0.36% |
| 64 MEBOOT | 121.056 | 121.678 | +0.51% | 113.434 | 113.251 | -0.16% |

Ordinary MEBOOT host medians are 15.10% / 9.19% below Basic. Absolute times
are higher than the earlier run (80.019 / 111.928 ms MEBOOT); both Basic and
MEBOOT shifted upward. This run does not identify the cause of the cross-run
shift, and should not overwrite the historical results. Negative GPU timing
changes do not imply negative event cost; observed changes include variation.

## Continuous GPU phase budget

Numbers are mean GPU event intervals in ms. Parentheses give each phase's
mean divided by the same path's mean total; this produces an additive budget.

| Phase | 32 Basic ms (%) | 32 MEBOOT ms (%) | 64 Basic ms (%) | 64 MEBOOT ms (%) |
|---|---:|---:|---:|---:|
| ScaleUp | 0.061 (0.06) | 0.040 (0.05) | 0.035 (0.03) | 0.034 (0.03) |
| ModUp | 2.728 (2.84) | 1.813 (2.26) | 2.264 (1.78) | 0.633 (0.56) |
| Trace | 0.001 (0.00) | 0.004 (0.00) | 0.002 (0.00) | 0.001 (0.00) |
| CtS | 40.289 (41.96) | 25.031 (31.22) | 52.925 (41.62) | 34.181 (30.13) |
| Split real/imaginary | 1.606 (1.67) | 1.624 (2.03) | 2.304 (1.81) | 2.361 (2.08) |
| EvalMod real | 14.435 (15.03) | 14.579 (18.18) | 20.607 (16.20) | 21.976 (19.37) |
| EvalMod imaginary | 14.492 (15.09) | 14.574 (18.17) | 20.722 (16.29) | 22.049 (19.44) |
| Recombine | 0.095 (0.10) | 0.098 (0.12) | 0.087 (0.07) | 0.136 (0.12) |
| StC | 22.285 (23.21) | 22.385 (27.92) | 28.225 (22.19) | 32.072 (28.27) |
| Finalize | 0.030 (0.03) | 0.011 (0.01) | 0.002 (0.00) | 0.001 (0.00) |
| Release temporaries | 0.001 (0.00) | 0.028 (0.03) | 0.001 (0.00) | 0.001 (0.00) |
| Total mean GPU | **96.023 (100)** | **80.188 (100)** | **127.174 (100)** | **113.444 (100)** |

ModUp includes the actual dense-to-sparse/sparse-to-dense handling applicable
to each path, projection and NTTs. Standard includes StD here; MEBOOT defers
switching to the main secret until CtS's LCR/AKS. Trace uses one accumulation
at full packing, so it performs no nontrivial trace rotations. Split includes
conjugation key switching, additions/subtraction and imaginary-unit multiply.
Recombine includes the final imaginary-unit multiply and addition. Temporary
lifetimes inside a phase contribute to that enclosing interval.

## Savings and remaining cost

CtS mean time falls by 15.257 ms (37.87%) for 32-bit and 18.744 ms (35.42%)
for 64-bit. ModUp saves another 0.915 / 1.631 ms. In 32-bit the subsequent
EvalMod/StC work is approximately equal. In 64-bit, MEBOOT retains an extra
level, so subsequent work uses a larger modulus basis: EvalMod together costs
2.696 ms more and StC costs 3.847 ms more. These costs are included rather
than lowering the MEBOOT output level to improve the comparison.

MEBOOT's EvalMod+CtS+StC account for 95.49% / 97.21% of the mean profiled
GPU time. The arithmetic model from the observed shares gives an upper bound
of ~18.2% / 19.4% total GPU-time reduction if both EvalMod calls were halved
and all other costs held fixed; this is a model, not a measured optimization.

## Independent CtS substage diagnostics

The same run also retained the separately synchronized M1–M4 diagnostics.
These medians are not the additive decomposition of the continuous CtS
interval above: synchronization, temporary lifetime and submission gaps differ.

| Stage | 32-bit median GPU ms | 64-bit median GPU ms |
|---|---:|---:|
| M1 | 0.758912 | 1.031152 |
| M2 LCR/AKS | 5.053555 | 6.665328 |
| M3 | 10.419750 | 14.794144 |
| M4 | 13.511750 | 18.150400 |

## Precision and depth

| Word | Basic SNR dB | MEBOOT SNR dB | Difference | Basic / MEBOOT max complex error |
|---|---:|---:|---:|---:|
| 32 | 128.881535 | 128.864156 | -0.017379 | 8.525357e-7 / 7.699732e-7 |
| 64 | 117.044706 | 117.037975 | -0.006731 | 3.286025e-6 / 3.379827e-6 |

32-bit CtS starts at 28/27 (Basic/MEBOOT) and both bootstraps end at level 13.
64-bit uses the same start and ends at 10/11. The key payload policy remains
the shared-store measurement. Whole-device memory peaks were not remeasured.

## Evidence and reproduction

```bash
bash unittest/tools/run_bootstrap_profile.sh
```

Evidence: `.scratch/meboot-performance/bootstrap-profile/final/`, including
raw `BOOT_PROFILE_STAGE`/`BOOT_PROFILE_TOTAL` CSV records in the benchmark log,
source state, environment, XML and exit code. The machine-readable phase
budget is `meboot_performance_test.profile.json`; ordinary latency/precision
is `meboot_performance_test.summary.json`.
