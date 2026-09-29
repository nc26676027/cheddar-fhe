# Spec: MEBOOT Core CtS Transformation Port to Cheddar Bootstrap

## Problem Statement

Users of Cheddar FHE requiring CKKS bootstrapping currently rely on standard butterfly-network linear transformations for CoeffsToSlots (CtS). This approach consumes multiple RNS modulus levels ($Q$) and demands a significant number of rotation keys, which limits the remaining multiplicative depth available for downstream computations after bootstrapping and puts heavy pressure on GPU memory bandwidth. Meanwhile, the MEBOOT architecture in Lattigo achieves state-of-the-art bootstrapping efficiency through a four-role decomposition combining Level-Conserving Rescaling (LCR), Aggregated Key-Switching (AKS), and structured BSGS folding. Users need these advanced CtS transformation mechanisms ported to Cheddar's GPU-accelerated CKKS pipeline so that bootstrapping consumes one fewer modulus level and operates with optimal BSGS rotation schedules while retaining high decryption precision.

## Solution

Port the core CtS transformation of MEBOOT into Cheddar as a modular, GPU-accelerated extension (`MEBootCtS`). In Phase 1, implement the full-ring four-stage $[4, 4, 3, 4]$ decomposition with dense BSGS bases $(64, 8)$:
1. Stage 1 ($m_1=4$): Full-ring ordinary BSGS linear transformation.
2. Stage 2 ($m_2=4$): Level-Conserving Rescaling (LCR) and Aggregated Key-Switching (AKS), preserving the current $Q$ modulus level without dropping a prime.
3. Stages 3 & 4 ($m_3=3, m_4=4$): Dense BSGS folded linear transformations evaluated using Cheddar's double-hoisted GPU infrastructure.
Support both 32-bit and 64-bit coefficient words via Cheddar's templated architecture, provide dedicated AKS key management in the interface, and validate correctness against golden reference matrices exported from the Lattigo MEBOOT implementation.

## User Stories

1. As an FHE application developer, I want Cheddar's CtS transformation to consume one fewer modulus level, so that my ciphertexts retain greater computational depth after bootstrapping.
2. As a Cheddar library user, I want MEBOOT CtS to be integrated as a modular option within the existing `BootContext`, so that I can switch between standard and MEBOOT bootstrapping without rewriting application code.
3. As a researcher, I want to run CtS with the exact $[4, 4, 3, 4]$ factorization and $(64, 8)$ dense BSGS bases, so that the GPU evaluation follows the theoretically optimal folding schedule established in MEBOOT.
4. As an FHE developer, I want an `AKSKeyMap` container separated from standard rotation keys, so that aggregated evaluation keys embedding matrix diagonals do not pollute or conflict with the general Galois key cache.
5. As a client generating evaluation keys, I want `UserInterface::PrepareAKSKeys` to compute AKS evaluation keys directly on the GPU from diagonal plaintexts, so that key generation remains fast and self-contained.
6. As a GPU computing engineer, I want the LCR centered division and modulus projection to execute entirely in a dedicated CUDA kernel between GPU-INTT and GPU-NTT, so that data never transfers back and forth between host and device during bootstrapping.
7. As a developer using 32-bit RNS moduli, I want `MEBootCtS` to support `uint32_t` words, so that I can use standard 30-bit bootstrapping parameter configurations.
8. As a developer using 64-bit RNS moduli, I want `MEBootCtS` to support `uint64_t` words, so that I can run high-precision 60-bit bootstrapping parameter configurations.
9. As an FHE test engineer, I want an isolated CtS unit test (`MEBootCtSTest`), so that I can verify output slot precision and level conservation before running full end-to-end bootstrapping.
10. As a performance engineer, I want M3 and M4 dense stages to reuse Cheddar's double-hoisted `HoistHandler`, so that matrix evaluation exploits GPU tensor parallelism and avoids redundant kernel launches.
11. As a verification engineer, I want C++ matrix planning to be validated against golden reference matrices exported from the Go `paper1-meboot` toolchain, so that matrix encoding errors are caught before executing homomorphic operations.
12. As a systems architect, I want Phase 1 to preserve the four-stage topology on the full ring, so that Phase 2 can seamlessly swap Stage 1 with a subring prefix operator without redesigning Stages 2–4.
13. As an end-to-end user, I want `BootContext::Boot` with MEBOOT CtS to pass all existing bootstrapping correctness checks in `unittest/Bootstrapping.cpp`, so that I am assured of zero regressions in decrypted message accuracy.

## Implementation Decisions

- **Architectural Seam**: Implement `MEBootCtS<word>` as a modular extension under `include/extension/MEBootCtS.h` and `src/extension/MEBootCtS.cu`, conditionally wired into `BootContext` via configuration.
- **Stage Factorization**: Adopt the $[4, 4, 3, 4]$ width decomposition for $N=2^{16}$ ($n=2^{15}$ slots) with dense BSGS bases $(b_3=64, b_4=8)$. Stage 1 runs on the full ring with standard Galois keys; Stage 2 executes LCR+AKS; Stages 3 and 4 execute folded dense BSGS.
- **Level-Conserving Rescaling (LCR)**: Compute the $c_0$ component via a dedicated `LCRProjectKernel` executing on GPU device vectors. The kernel performs centered integer division by $q_L$ and projects the small integer back onto the $q_L$ modulus ring, positioned between `ntt_handler_.INTT` and `ntt_handler_.NTT`.
- **Aggregated Key-Switching (AKS)**: Manage M2 evaluation keys via a dedicated `AKSKeyMap` container. Provide `UserInterface::PrepareAKSKeys` to encode matrix diagonals into the evaluation keys on device. The $c_1$ component is evaluated in $QP$ and reduced via `ModDownQPtoQNTT` without dropping a $Q$ prime.
- **Dense BSGS Reuse**: Map M3 and M4 diagonal vectors into Cheddar's `StripedMatrix` format, executing via the existing `LinearTransform` and double-hoisted `HoistHandler`.
- **Dual Matrix Verification**: Export reference diagonal polynomials from Go (`paper1-meboot`) into a test asset. Implement `MEBootMatrixPlanner` in C++ and verify byte-level and numerical consistency against the reference asset in unit tests.
- **Generic Word Support**: Implement `MEBootCtS<word>` as a template supporting `uint32_t` and `uint64_t`, utilizing Cheddar's `DoubleWord` primitives for wide-integer arithmetic.

## Testing Decisions

- **Component Seam (Isolated CtS Test)**: In `unittest/MEBootCtSTest.cpp`, initialize `MEBootCtS` with known input ciphertexts, perform the forward CtS transformation, decrypt and decode slots, and assert that:
  - Max error between decrypted slots and expected message is bounded within the target SNR threshold.
  - Output ciphertext remains at the exact target level (preserving 1 additional $Q$ level compared to standard CtS).
- **End-to-End Seam (Bootstrapping Test)**: In `unittest/Bootstrapping.cpp`, execute full `BootContext::Boot` runs comparing `ct_res` against input messages for both 32-bit and 64-bit parameter configurations (`bootparam_40.json` and `bootparam_40_64bit.json`).
- **Matrix Consistency Test**: In `unittest/MEBootMatrixPlannerTest.cpp`, compare matrices generated by `MEBootMatrixPlanner` with golden reference arrays exported from Go.
- **Testing Standard**: Tests verify observable cryptographic behavior (output slot values, ciphertext metadata, noise bounds) rather than private internal buffer states.

## Out of Scope

- Subring prefix execution on GPU (M1 subring drop/sampling), which is scheduled for Phase 2.
- SlotToCoeff (StC) MEBOOT modifications (Phase 1 focuses exclusively on CtS).
- Hardware-specific kernel fusion across stages beyond the dedicated LCR projection kernel.
- Dynamic search optimizer in C++ (the runtime uses fixed, proven optimal presets like $[4, 4, 3, 4]/(64, 8)$).

## Further Notes

- References:
  - Architecture decisions: `docs/adr/0001-meboot-cts-phased-integration.md`, `docs/adr/0002-aks-and-lcr-gpu-design.md`, `docs/adr/0003-stage-decomposition-and-matrix-verification.md`.
  - Domain glossary: `CONTEXT.md`.
  - Theory and reference implementation: `d:/code space/paper1-linear-bootstrap/paper/linear-bootstrap/tools/meboot/`.
