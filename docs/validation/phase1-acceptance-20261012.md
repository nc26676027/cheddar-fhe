# MEBOOT CtS Phase 1 acceptance — 2026-10-12

Status: accepted within the owner-approved ADR-0004 input contract. The final
serial run completed with 31 passed, 0 failed and 2 expected unsupported-profile
skips across 33 declared cases. The separate negative Golden experiment was
also successful: its intentional corruption produced the required test failure.

This is correctness/depth acceptance, not performance acceptance. The expanded
`phase1-performance-assessment-20261012.md` records Basic latency increases and
the approximately 31 dB lower observed 32-bit SNR relative to standard bootstrap.

## Accepted scope

The owner accepted ADR-0004 during this session. Phase 1 uses the four-stage
`[4,4,3,4]` CtS, dense bases `(64,8)`, a Subring Prefix under a sparse secret
supported on coefficient indices divisible by 16, and full-size ciphertexts
and NTTs. Stage 2's single-digit AKS switches to the main secret and LCR
preserves its input level. Full packing and first width 4 are required.

The input must satisfy the centered small-component bounds documented on
LCROperator. Ordinary main-secret ciphertexts and uniform full-Q components
from ordinary key switching are outside the accepted input contract. Ring
dimension drop, reduced-size GPU NTTs, StC changes, and performance optimization
remain outside this acceptance.

The original ordinary-BSGS Stage 1 assumption was tested rather than silently
discarded. A new main-secret test failed on the original prefix; ordinary BSGS
then passed Stage 1 but failed the subsequent LCR stage for all three tested
CtS profiles. The owner chose the supported contract. The attempted patch and
failure logs are preserved in `.scratch/phase1-acceptance/logs/ordinary-bsgs/`,
with the isolated initial failure in `stage1-red.log`.

## Environment and source

- Source base: `16dc80bf7e4212cddb82ac07cd59589f81d42947`, branch `feat/meboot-cts`,
  with the acceptance changes in the working tree.
- WSL Ubuntu, x86_64; CUDA compiler 12.8.93; CMake 3.31.6; Release build.
- NVIDIA GeForce RTX 5060 Ti, 16311 MiB, driver 591.86.
- Exact environment, working-tree patch, source hashes, configure/build logs,
  commands, exit codes, and GoogleTest XML: `.scratch/phase1-acceptance/logs/`.

## Verification

| Gate | Evidence / result |
|---|---|
| Complete independent Golden comparison | Both word types passed; 3,047,424 complex coefficients each; maximum complex error 1.23083e-16 against tolerance 1e-12 |
| AKS containers and key generation | 4 tests passed |
| LCR precision and level conservation | 5 tests passed; complex random inputs now use the intended range [-1,1] |
| Prefix and isolated CtS | 7 tests passed, including prefix contract, 32-bit 30/40-bit-scale and 64-bit 40-bit-scale profiles; output level 5 -> 2 |
| Linear transformation regression | 3 tests passed |
| Autonomous modulus switching | 3 tests passed, covering every level of the 30/35/40-bit-scale profiles |
| Standard and MEBOOT bootstrap | 6 tests passed, 2 expected skips; all four standard profiles and both supported MEBOOT profiles passed |
| Later-slot imaginary-sign mutation | Sign flip at stage 0, offset 0, slot 2048 rejected (complex error 4.79369e-5); original fixture restored with identical SHA-256 |

Isolated CtS uses component-wise tolerances of `5e-3` for the 30-bit-scale
profile and `1e-4` for both 40-bit-scale profiles, tightened from `0.05`.
Measured final-output complex errors were `1.85701e-3`, `1.95222e-6`, and
`1.31456e-6`, respectively. Real/imaginary splitting is also checked. Message
comparison now fails on non-finite errors.

End-to-end precision uses the existing component-wise `1e-3` threshold. The
MEBOOT output levels are asserted as 13 (32-bit) and 11 (64-bit), with both
Basic and MinKS entry paths tested. These are correctness checks; no throughput
improvement or statistical equality of SNR across independent random runs is
claimed.

| MEBOOT profile | Output level | Max complex error | SNR (power-ratio dB) |
|---|---:|---:|---:|
| 32-bit Basic | 13 | 5.29319e-5 | 98.08 |
| 32-bit MinKS | 13 | 5.44203e-5 | 97.96 |
| 64-bit Basic | 11 | 2.80604e-6 | 120.18 |
| 64-bit MinKS | 11 | 2.51054e-6 | 120.01 |

The two skipped cases are `MEBootBootstrap/bootparam_30_json` and
`MEBootBootstrap/bootparam_35_json`: those MEBOOT parameter profiles are not
configured by Phase 1. Their standard bootstrap cases ran and passed. The
summarizer requires the supported 32/64-bit CtS and MEBOOT bootstrap cases to
have run, checks every binary's exit code, and distinguishes these skips from
passes. See `.scratch/phase1-acceptance/summary.json` for machine-readable
results and all logged error/SNR measurements.

## Golden provenance and reproduction

`parameters/meboot_golden_4434.bin.gz` contains all diagonal coefficients,
not samples. The independent Go module is `paper1-meboot` in the sibling
`paper1-linear-bootstrap` repository, revision
`a4415572d9557c38b92cebe0da56a35bea705c54`, clean at export. The exporter uses
Lattigo `dft.MatrixLiteral.GenMatricesSlice` at 128-bit precision. Go's per-stage
normalization is `1/16`; both real and imaginary parts, signs, zeros and rotation
offsets are compared. Source/exporter/fixture hashes and format settings are
recorded in `parameters/meboot_golden_4434.manifest.json`.

The frozen fixture SHA-256 is
`80c3ec27ece748814aac6467420ea1801289ab36165f8e4186569566a3785d1a`.
Cross-language floating-point byte identity is not required; fixture bytes are
pinned and coefficient values are compared numerically.

```bash
bash unittest/tools/run_phase1_acceptance.sh
python3 unittest/tools/verify_golden_guard.py
python3 unittest/tools/summarize_phase1_acceptance.py
```

The six extension binaries run in full. The basic binary runs
`AutonomousModSwitchOperations` across all profiles; the full basic arithmetic
and encoding suite is outside this acceptance scope. CTest discovers all seven
binaries. GitHub issue state was not changed because `gh` is unavailable;
local tickets are the configured fallback. This acceptance does not merge the
branch or begin Phase 2.
