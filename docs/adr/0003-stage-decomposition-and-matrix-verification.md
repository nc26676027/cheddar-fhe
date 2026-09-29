# Stage Decomposition, Template Generics, and Golden Matrix Reference

## Context
Implementing Phase 1 of MEBOOT CtS in Cheddar requires resolving three concrete engineering aspects: the stage factorization widths for $N=2^{16}$ ($n=2^{15}$), generic support for 32-bit and 64-bit coefficient words, and a foolproof method to verify matrix diagonal accuracy between Go and C++.

## Decision
1. **Factorization Topology**: We adopt the four-stage $[4, 4, 3, 4]$ decomposition with dense BSGS bases $(b_3=64, b_4=8)$. Stage 1 runs as an ordinary BSGS stage on the full ring during Phase 1; Stage 2 executes LCR+AKS level conservation; Stages 3 and 4 execute folded dense BSGS.
2. **Template Generics**: We implement `MEBootCtS<word>` as a full template for both `uint32_t` and `uint64_t`, utilizing Cheddar's `DoubleWord` arithmetic for centered division and modular projection.
3. **Dual Matrix Validation**: We use the Go codebase (`paper1-meboot`) to export the exact reference matrices and diagonal coefficients for $[4, 4, 3, 4]$ into a JSON/binary test asset. In parallel, we implement the C++ `MEBootMatrixPlanner`, verifying its output against the exported golden reference in unit tests before evaluating homomorphic ciphertext operations.

## Consequences
- Preserves architectural parity with MEBOOT's theoretical model, ensuring a zero-refactor transition when Subring (Phase 2) is later enabled on Stage 1.
- Guarantees compatibility with all of Cheddar's existing parameter sets (32-bit and 64-bit).
- Eliminates silent matrix encoding errors early through automated golden comparison.
