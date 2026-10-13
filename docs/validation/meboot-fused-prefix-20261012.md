# Fused MEBOOT Subring Prefix — 2026-10-12

The M1 prefix previously applied 16 plaintext diagonals through separate
full-ciphertext permutations, products and additions. Its lack of key switching
did not remove those intermediate buffers and repeated global-memory traffic.

`SubringPrefixKernel` now gathers the automorphism source, multiplies the
output-position diagonal and accumulates all 16 terms in registers for both
components. It writes each output component once. The Galois factors, diagonal
pointers, Q primes and Montgomery inverses are uploaded during preparation,
with no per-evaluation metadata transfer. The existing final Rescale is retained.
The `[4,4,3,4]` fold, subring input contract, gains, scales and level schedule
are unchanged. No dimension drop is introduced.

## Verification method

`FusedPrefixMatchesUnfused` reconstructs the previous evaluator using public
Permute, Mult, Add and Rescale operations. It compares every output residue of
both components exactly, alongside scale, level and slot metadata. Inputs
exercise zeros, residues near q-1 and varying positions; both word types, level
5, a bootstrap-sized Q basis and input/output aliasing are covered.

The paired benchmark retains identical inputs and keys per word type. Complete
bootstrap measurements and individual stage measurements now both use five
warmups and 30 timed executions in the final run. Stage times include the
stage's Rescale where applicable. Setup, encoding and key generation are
excluded. These are repeated executions under one key set per word type.

Evidence is archived separately in
`.scratch/meboot-performance/m1-fused/final/`; the previous unfused-prefix
results remain in `.scratch/meboot-performance/final/` and
`docs/validation/meboot-quality-and-latency-20261012.md`.

Only the final serial runner is used for acceptance claims. The exploratory
probe contains a zero-test filtered invocation and only three stage samples;
neither is used as final correctness or stage-latency evidence.

## Final results

All seven binaries finished with exit code zero: 33 cases passed, zero failed,
and two unsupported MEBOOT parameter profiles were skipped. All three exact
prefix differential cases passed. The unchanged independent Golden comparison
also passed. The paired precision and Basic latency target checks passed for
both word types.

| Word | Earlier M1 GPU ms | Fused M1 GPU ms | M2 GPU ms | Fused M1 / M2 |
|---|---:|---:|---:|---:|
| 32-bit | 6.084830 | 1.701055 | 5.274305 | 32.25% |
| 64-bit | 7.129216 | 1.039984 | 5.937632 | 17.52% |

The historical M1 medians used three samples; the final medians use five
warmups and 30 samples. The observed M1 reductions are 72.04% and 85.41%.
The initial 32-bit exploratory probe was faster (~0.74 ms); the final
1.70 ms figure is used here rather than selecting the best observation.
The timings encompass the whole stage, including allocation/submission gaps
inside the CUDA-event interval and Rescale, not just the fused kernel.

| Word | M1 | M2 | M3 | M4 |
|---|---:|---:|---:|---:|
| 32-bit GPU ms | 1.701055 | 5.274305 | 10.341400 | 13.344650 |
| 64-bit GPU ms | 1.039984 | 5.937632 | 13.256880 | 16.968192 |

| Word | Basic host median ms | MEBOOT host median ms | Change | Basic / MEBOOT GPU median ms |
|---|---:|---:|---:|---:|
| 32-bit | 94.306600 | 80.019050 | -15.15% | 87.318300 / 72.641350 |
| 64-bit | 123.128256 | 111.928348 | -9.10% | 117.052498 / 104.524849 |

The host ranges are 93.3859–95.4541 / 78.9962–80.9650 ms (32-bit
Basic/MEBOOT), and 122.855101–125.155910 / 111.250290–112.535444 ms
(64-bit). Relative to the earlier MEBOOT host medians of 84.25855 and
117.281261 ms, the final medians are lower by 5.03% and 4.56%; these are
separate runs, not paired before/after samples.

| Word | Basic SNR dB | MEBOOT SNR dB | Difference | Basic / MEBOOT maximum complex error |
|---|---:|---:|---:|---:|
| 32-bit | 128.849972 | 128.933869 | +0.083897 | 8.342138e-7 / 8.181484e-7 |
| 64-bit | 119.111253 | 119.124678 | +0.013426 | 2.644558e-6 / 2.593374e-6 |

The 64-bit absolute SNR differs from the earlier run because keys/ciphertexts
were regenerated; precision claims use the within-run paired comparison. The
32-bit path still starts CtS at level 27 versus Basic 28 and ends at 13. The
64-bit path still ends at 11 versus Basic 10 at the same starting level.

M1 now corresponds to approximately 2.3% / 1.0% of the complete MEBOOT GPU
duration (the stages are measured separately, so this is a scale comparison,
not an additive profiler attribution). It is a small remaining cost, not zero:
the full-ring prefix must still read encoded diagonals and perform Rescale.

Used-key payloads are unchanged: Basic/MEBOOT 4.047852 / 4.872559 GiB for
32-bit, and 4.431641 / 5.400391 GiB for 64-bit. Whole-device memory peaks were
not remeasured in this run; the earlier NVML results are historical only.

## Reproduction

```bash
bash unittest/tools/run_meboot_quality.sh .scratch/meboot-performance/m1-fused/final
```

Environment: RTX 5060 Ti, WSL Ubuntu, CUDA 12.8.93, Release, full packing with
N=65536 and 32768 slots, default scale 2^40. The evidence directory contains
commands, logs, XML, source hashes/patch, environment and the machine-readable
`meboot_performance_test.summary.json`. An end-of-run source snapshot records
the added documentation comment; no semantic implementation changed after
the tested build.
