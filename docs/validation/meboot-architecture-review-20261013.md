# MEBOOT integration architecture review and fixes, 2026-10-13

Review baseline: 8df8b26...edfe0b7; implementation base edfe0b7.
The owner selected fixing the standard SSE regression, then sharing centered
CRT reconstruction. This report covers those two fixes and the centered
single-prime ModUp correction required to restore standard Bootstrap correctness.

## Review findings

Spec axis: one P1 finding. UserInterface::PrepareSecrets restricted ALL sparse
secrets to multiples of 16, including ordinary Context and standard Bootstrap.
Parameter allows sparse_h <= dense_h, but the sampler population shrank to N/16.
N=65536, sparse_h=8192 reproduces an Invalid length exit with the old sampler
domain. The original full-ring domain succeeds. This conflicts with spec
0001/0002 zero-regression requirements; ADR-0004 specifies MEBOOT inputs only.

Standards axis: three judgment findings, no confirmed hard-standard breach.
The duplicated AKS/LCR centered CRT arithmetic is addressed here.
SplitRealImag remains test-only while BootImpl has its own split sequence;
the unused GetMutableAKSKeyMap remains a nonblocking interface-width item.
Explicit AKS overloads are compatibility adapters required by architecture
ticket 04, so they were not removed. Arbitrary main-secret CtS and Phase 2
dimension drop remain outside the accepted ADR-0004 contract.

## Cheddar reuse

M3/M4 reuse StripedMatrix and LinearTransform double hoisting. LCR uses the
existing NTTHandler, ModSwitchHandler and Basic Montgomery operations.
AKS retains EvaluationKey, the existing encryption kernel and EvkMap storage.
Shared arithmetic is now in Basic.cuh; the specialized fused M1/M2 kernels
and their device dataflow are retained. RMM, GPU NTT kernels, BSGS schedules
and the StC optimization are unchanged by these fixes.

## Sparse-secret selection

UserInterface(context) restores full-ring sampling for ordinary Context and
standard BootContext, and automatically selects Subring Prefix for a MEBOOT
BootContext. The explicit SparseSecretLayout constructor selects the latter
for standalone CtS or same-secret paired benchmarks. Invalid Subring Prefix
weights above N/16 are rejected before secret sampling; an explicit full-ring
layout is rejected for a MEBOOT BootContext. PrepareMEBootAKSKeys also checks
the client layout. Secret polynomials remain private.

SubringTestbed reuses the existing fixture and explicitly selects the layout
for CtS and paired measurements. Standard Bootstrap tests use full-ring SSE;
the paired benchmark uses identical Subring Prefix secrets for both paths.

## Shared centered CRT

Basic::ReconstructCenteredCRT replaces ReconstructCenteredAKS and
ReconstructCenteredMultiLimb. The extracted LCR argument list and function
body were compared against edfe0b7 and are unchanged apart from the name.
AKS and LCR now invoke that same device-inline implementation.
This is bounded reconstruction from the first 1/2/3 limbs for 32-bit words
or up to 2 limbs for 64-bit words; it is not a general replacement for ModUp.

GPU boundary tests reconstruct 152 signed integer cases across both words
and 1/2/3 available-limb configurations, checking sign and both 64-bit halves
against independently supplied integers. Cases include zero, +/-1, centered
range endpoints, cross-limb magnitudes and deterministic interior values.

## Final validation

RTX 5060 Ti, CUDA 12.8, Release, N=65536; GPU processes ran serially.
Complete rebuild succeeded. Final XML contains 39 passes, zero failures and
two pre-existing expected skips. Commands, XML, logs, source hashes and the
paired numerical records are preserved in
`.scratch/meboot-performance/architecture-review/validated/`.

Initial failures and red tests remain in the adjacent final/ directory.

Library/test/parameter hashes were checked against the frozen run snapshot.
Only ModUpToMaxTest.cpp changed its reference construction from a threshold
calculation to explicitly supplied signed integers; its input cases are identical.
That target was rebuilt and rerun successfully. The final source-end hashes
verify successfully and include this latest test source.

| Target | Passed | Failed | Skipped |
|---|---:|---:|---:|
| user_interface_test | 2 | 0 | 0 |
| centered_crt_test | 2 | 0 | 0 |
| mod_up_to_max_test | 2 | 0 | 0 |
| meboot_matrix_planner_test | 3 | 0 | 0 |
| aks_key_test | 4 | 0 | 0 |
| lcr_step_test | 5 | 0 | 0 |
| meboot_cts_test | 10 | 0 | 0 |
| linear_transform_test | 3 | 0 | 0 |
| boot_test | 6 | 0 | 2 |
| meboot_performance_test | 2 | 0 | 0 |

The new UserInterface tests construct a standard client at sparse_h=N/8
(8192, above the former 4096 capacity) and verify sparse decryption for both
word sizes. The negative Subring Prefix capacity tests use a re-executed
subprocess: the first fork-based version failed CUDA runtime initialization.
That failure was preserved separately and the corrected test reran successfully.
The runner initially returned failure for those two tests; final evidence
includes the successful resumed client command and aggregate verification.

## Single-prime ModUp follow-up

Restoring full-ring sparse sampling uncovered a 64-bit standard Bootstrap
accuracy failure with approximately 0.1 maximum error. The standard path
does not call the shared CRT primitive. Existing ModUpToMax1 lifted into
[0,q0), while ModUpToMax2 already used centered representatives.
A deterministic test through ElementWiseHandler reproduced the single-prime
inconsistency for both words at coefficients just above q0/2.
ModUpToMax1 now uses the same centered rule and existing Basic::Negate;
source q0 residues are preserved exactly. No additional kernels, modulus
levels or altered EvalMod polynomial were introduced. Both red cases turned
green. Standard 64-bit Basic and MinKS then passed three consecutive fresh
secret/input runs before the complete final regression.

Capacity 8192 is tested for client construction/decryption; Bootstrap
precision is tested with the configured sparse weight 32.

## Paired precision and performance

Five warmups and 30 alternating samples per path; shared input, secrets and
keys. The existing precision alarm and saved-level assertions pass.

| Word | Basic host median ms | MEBOOT host median ms | Change | Basic SNR dB | MEBOOT SNR dB |
|---|---:|---:|---:|---:|---:|
| 32 | 84.23130 | 71.12940 | -15.55% | 128.90986 | 128.95485 |
| 64 | 113.69329 | 103.39991 | -9.05% | 115.19662 | 115.14304 |

| Word | M1 CUDA median ms | M2 CUDA median ms | M3 CUDA median ms | M4 CUDA median ms |
|---|---:|---:|---:|---:|
| 32 | 0.74338 | 4.93286 | 9.46593 | 12.65835 |
| 64 | 1.04413 | 5.05630 | 11.56050 | 14.88786 |

These are same-run comparisons. Historical absolute medians are not used
to attribute a speed change to this refactor. No new GPU memory peak or
complete Bootstrap profile was measured: the relevant profiling and StC
dataflow are unchanged, and the earlier report remains separate.

DeepSeek V4.1 Flash handled baseline evidence extraction and the mechanical
CRT move; the main agent inspected the patch and verified its function body.
Reproduce with `bash unittest/tools/run_meboot_quality.sh <evidence-dir>`.
