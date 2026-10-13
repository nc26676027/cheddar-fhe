# 04: Four-Stage MEBootCtS Module and Slot Accuracy

**What to build:**
A complete `MEBootCtS<word>` module coordinating all four stages ($[4, 4, 3, 4]$) of the CtS linear transform in full-size ciphertext storage (Stage 1 Subring Prefix under the accepted sparse-secret contract in ADR-0004, Stage 2 LCR+AKS, Stages 3 & 4 folded dense BSGS via `LinearTransform`, followed by `SplitRealImag`). Includes a standalone unit test verifying decrypted slot accuracy against target plaintexts and demonstrating that the output ciphertext saves 1 modulus level compared to four ordinary rescaling stages.

**Blocked by:** 01: Matrix Planner and Golden Reference Verification, 03: LCR Projection Kernel and Stage 2 Operator

**Status:** completed

- [x] `MEBootCtS<word>` class implemented in `include/extension/MEBootCtS.h` and `src/extension/MEBootCtS.cu`, templated for `uint32_t` and `uint64_t`
- [x] Stages 3 and 4 integrated with Cheddar's double-hoisted `LinearTransform` using dense BSGS bases $(64, 8)$
- [x] Unit test in `unittest/MEBootCtSTest.cpp` evaluates CtS on contract ciphertexts under the subring-supported sparse secret, decrypts slots, and asserts precision, output level, and real/imaginary splitting

Verification on 2026-10-12: all 7 isolated prefix/CtS tests passed on RTX 5060 Ti
with CUDA 12.8. Output level 5 -> 2; tightened component-wise tolerances are
5e-3 for the 30-bit-scale profile and 1e-4 for both 40-bit-scale profiles.
See `../../../docs/validation/phase1-acceptance-20261012.md` and the recorded
`meboot_cts_test.log` / XML under `.scratch/phase1-acceptance/logs/`.
