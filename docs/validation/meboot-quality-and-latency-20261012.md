# MEBOOT precision and Basic latency — final results, 2026-10-12

These results precede the M1 fusion. The subsequent measurements and exact
prefix regression are in [Fused Subring Prefix](meboot-fused-prefix-20261012.md).
The tables below remain archived evidence for the earlier implementation.

The agreed targets are met for the two tested profiles on RTX 5060 Ti: retain
the CtS level saving, recover standard 32-bit precision near 129 dB, and match
or beat standard Basic steady-state latency. The accepted ADR-0004 input
contract is unchanged. This result does not establish key-memory reduction.

## Paired measurements

Both paths use the same encrypted input, main/subring secrets and evaluation
key store. Each path has five warmups and 30 timed evaluations, alternating
execution order. Preparation, key generation and encryption/decryption are
outside the timed region. CUDA events and synchronized host timing are both
recorded. The deterministic message combines real/imaginary sinusoids of
amplitude 0.7; the encryption/key randomness remains real.

| Word | Basic median host ms | MEBOOT median host ms | Change | Basic / MEBOOT median GPU ms |
|---|---:|---:|---:|---:|
| 32-bit | 93.97045 | 84.25855 | -10.34% | 87.7476 / 77.6064 |
| 64-bit | 123.114254 | 117.281261 | -4.74% | 117.149139 / 110.063534 |

The 30-sample host ranges were 93.1257–95.1282 / 83.2525–85.9402 ms for
32-bit Basic/MEBOOT and 122.321968–124.445349 / 116.229302–117.819317 ms for
64-bit. These are repeated executions under one key set per word type, not
30 independent precision/key-generation experiments.

| Word | Basic SNR dB | MEBOOT SNR dB | Difference | Basic / MEBOOT max complex error |
|---|---:|---:|---:|---:|
| 32-bit | 128.877993 | 128.968355 | +0.090362 | 8.31460e-7 / 7.46972e-7 |
| 64-bit | 116.519956 | 116.472145 | -0.047810 | 3.52853e-6 / 3.39226e-6 |

The approximately 32 dB paired 32-bit regression is eliminated. The final
relative differences are within 0.1 dB. The 64-bit absolute baseline varies
with the generated ciphertext/keys; no claim of 129 dB for that profile is
made. Earlier independent-key trials also retained near-standard precision.

The 32-bit path retains output level 13 while starting CtS one level lower
(27 versus 28). The 64-bit path retains output level 11 versus standard 10
at the same starting level. Latency comparisons include these depth policies;
they do not silently remove the saved level to match work.

## Implementation and diagnosis

The original paired test measured standard 128.82 dB versus MEBOOT 96.75 dB.
Increasing Stage 2's gain from 64 to 65536, with compensation in the dense
stages, restored precision. A subsequent allocation uses Stage 1 gain f^(1/4)
and dense gains f^(3/8)/256 each, so their product with the Stage 2 gain is
still f. This preserves the intended linear transformation and level schedule.

The M2 bottleneck was its repeated products, permutations, copies and additions
over large buffers. A fused GPU kernel now accumulates both AKS components in
QP and the plaintext branch in Q, retaining the existing ModDown and centered
projection. Its gather mapping matches ElementWise::Permute. Montgomery
constant multiplication is used for modular projection without changing the
integer result. Individual M2 GPU medians are now 4.91619 ms (32-bit) and
5.920224 ms (64-bit), compared with representative pre-fusion observations
near 23/26 ms. Stage timings are separate from the full-bootstrap benchmark.

The new paired benchmark initially released the last-installed RMM pool before
the standard context/key allocations that used it. Exception-safe cleanup now
releases those allocations first. The combined 32/64-bit run completes; the
production MemoryPool implementation was not changed.

## Memory measurements

| Word | Basic used-key payload GiB | MEBOOT used-key payload GiB | Change |
|---|---:|---:|---:|
| 32-bit | 4.047852 | 4.872559 | +20.37% |
| 64-bit | 4.431641 | 5.400391 | +21.86% |

These sums use each path's referenced evaluation-key shape metadata in the
shared benchmark store. Shared rotations can be provisioned at the greater
of the paths' requested levels. The metric excludes plaintext matrices,
scratch, allocator reserve and driver memory; it is not a standalone deployment
key-set measurement or peak device allocation.

Whole-device memory.used was sampled every 500 ms during standalone correctness
cases, with complete time coverage for the compared 40-bit-scale cases:

| Word | Standard observed device peak MiB | MEBOOT observed device peak MiB |
|---|---:|---:|
| 32-bit | 8001 | 8044 |
| 64-bit | 9102 | 9038 |

These sampled peaks include driver/background processes and allocator reserve
and can miss short allocation peaks. The two-context paired test observed
12300 / 13934 MiB for 32/64-bit. The observations support roughly similar
overall memory use in the standalone tests, not a proven memory reduction.

## Validation and reproduction

Final relevant regression: 30 passed, 0 failed, 2 expected unsupported MEBOOT
profile skips, across all six extension suites and the two paired cases.
Complete Golden tests still compare 3,047,424 complex coefficients per word
type. The previously passed three all-level basic ModSwitch cases were not
repeated because their implementation was unchanged.

```bash
bash unittest/tools/run_meboot_quality.sh
```

The runner saves source hashes/patch, environment, exact commands, exit codes,
GoogleTest XML, and raw metrics to `.scratch/meboot-performance/final/`, and
requires both precision and Basic latency targets to pass. Machine-readable
results are `meboot_performance_test.summary.json` and `memory-summary.json`;
the memory helper consumes the separately recorded NVML telemetry.

Environment: WSL Ubuntu, RTX 5060 Ti 16311 MiB, driver 591.86, CUDA 12.8.93,
CMake 3.31.6, Release. Source base remains 16dc80b plus the working-tree changes.
The conclusions are scoped to these profiles, hardware, input contract and
measured workload; Phase 2 ring dimension drop has not begun.
