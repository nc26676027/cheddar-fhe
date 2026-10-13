# Phase 1 performance assessment — 2026-10-12

Correctness and one fewer CtS level are verified under ADR-0004's input
contract. Broad runtime acceleration, preserved 32-bit precision relative to
standard bootstrap, and reduced total key memory are not established.
The existing spec has no numerical latency, throughput, key-memory, or
relative-SNR acceptance target.

## Recorded end-to-end latency

Source: `.scratch/phase1-acceptance/logs/boot_test.log`. Hardware: RTX 5060 Ti
(16311 MiB), WSL Ubuntu, CUDA 12.8.93, Release; N=65536, 32768 slots.
The profiler warms up five executions and times the sixth bootstrap execution
with host wall-clock synchronization overhead. Input encryption is completed
before the final timer starts. Matrix preparation, key generation, decryption,
and output verification are outside this timed region.

| Word / path | Standard (ms) | MEBOOT (ms) | MEBOOT latency change |
|---|---:|---:|---:|
| 32-bit Basic | 101.313 | 119.432 | +17.88% |
| 32-bit MinKS | 125.995 | 119.858 | -4.87% |
| 64-bit Basic | 136.885 | 152.166 | +11.16% |
| 64-bit MinKS | 178.346 | 170.268 | -4.53% |

These are single timed observations per path, not sample means or medians.
The apparent MinKS advantage cannot establish a stable 4–5% improvement without
repeated, interleaved measurements and variability estimates. Basic is the
faster standard path in these observations; MEBOOT does not beat that baseline.

The comparisons also implement different depth policies: 32-bit MEBOOT starts
CtS at level 27 rather than standard level 28, keeping the same output level 13;
64-bit MEBOOT keeps the same starting level and yields one additional output
level (11 rather than 10). Inputs and secrets were generated independently
for the two test fixtures. Results therefore compare the tested configurations,
not an isolated algorithm substitution at equal input, output depth and error.
MEBOOT's CtS ignores `min_ks` in its rotation requests and dense-stage execution;
its MinKS label applies to the overall bootstrap path, not a separate MinKS
optimization of its CtS stages.

## Precision comparison

| Word / path | Standard max complex error | MEBOOT max complex error | Standard SNR (dB) | MEBOOT SNR (dB) |
|---|---:|---:|---:|---:|
| 32-bit Basic | 1.05807e-6 | 5.29319e-5 | 129.00 | 98.08 |
| 32-bit MinKS | 8.91271e-7 | 5.44203e-5 | 129.07 | 97.96 |
| 64-bit Basic | 3.18112e-6 | 2.80604e-6 | 118.63 | 120.18 |
| 64-bit MinKS | 3.33861e-6 | 2.51054e-6 | 118.90 | 120.01 |

All pass the existing component-wise 1e-3 correctness gate. However, 32-bit
MEBOOT's observed maximum complex error is 50–61 times the standard value,
and SNR is about 31 dB lower. Passing an absolute tolerance does not establish
relative precision preservation. The 64-bit observations show similar or
slightly better error/SNR, with the same single-run limitation. SNR here is
10*log10(signal-power/error-power), not a guaranteed per-slot precision bit count.

Isolated CtS final-output complex errors were 1.85701e-3 (32-bit 30-bit scale),
1.95222e-6 (32-bit 40-bit scale), and 1.31456e-6 (64-bit 40-bit scale). Their
respective component-wise tolerances are 5e-3, 1e-4 and 1e-4. These experiments
use contract ciphertexts and different output amplitudes; they must not be
interpreted as equal-work precision comparisons between configurations.

## Completed validation and remaining evidence

The final acceptance run had 31 passes, 0 failures, and 2 explicitly unsupported
MEBOOT-profile skips. Complete Golden comparisons cover all 3,047,424 complex
coefficients for each word type, with maximum error 1.23083e-16. A later-slot
imaginary-sign mutation was rejected. LCR preserves its level; isolated CtS
consumes three levels for four stages, proving the one-level structural saving.

No saved evidence quantifies total evaluation-key bytes, AKS versus rotation
key bytes, steady-state/peak device allocation, key-generation cost, individual
CtS stage latency, or repeated-throughput statistics. The environment's idle
`nvidia-smi` snapshot is not a key-memory or peak-memory measurement. GoogleTest
case durations include setup and verification and are not bootstrap latency.

Before performance optimization, settle the desired trade-off between saved
levels, precision, latency and key memory. A subsequent benchmark should reuse
inputs, report depth and error beside latency, alternate standard/MEBOOT order,
record repeated host and CUDA-event timings, and separate initialization/key
generation from steady-state evaluation. The 32-bit precision difference needs
investigation before making an equal-precision speed claim.
