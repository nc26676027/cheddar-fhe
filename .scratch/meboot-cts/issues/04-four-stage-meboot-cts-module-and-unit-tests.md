# 04: Four-Stage MEBootCtS Module and Slot Accuracy

**What to build:**
A complete `MEBootCtS<word>` module coordinating all four stages ($[4, 4, 3, 4]$) of the CtS linear transform on full-ring ciphertexts (Stage 1 standard BSGS, Stage 2 LCR+AKS, Stages 3 & 4 folded dense BSGS via `LinearTransform` and `HoistHandler`, followed by `SplitRealImag`). Includes a standalone unit test verifying decrypted slot accuracy against target plaintexts and demonstrating that the output ciphertext saves 1 modulus level compared to standard CtS.

**Blocked by:** 01: Matrix Planner and Golden Reference Verification, 03: LCR Projection Kernel and Stage 2 Operator

**Status:** ready-for-agent

- [ ] `MEBootCtS<word>` class implemented in `include/extension/MEBootCtS.h` and `src/extension/MEBootCtS.cu`, templated for `uint32_t` and `uint64_t`
- [ ] Stages 3 and 4 integrated with Cheddar's double-hoisted `HoistHandler` using dense BSGS bases $(64, 8)$
- [ ] Unit test in `unittest/MEBootCtSTest.cpp` evaluates CtS on encrypted random test messages, decrypts slots, and asserts that slot error is within tolerance while the ciphertext level is preserved
