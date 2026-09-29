# 03: LCR Projection Kernel and Stage 2 Operator

**What to build:**
A dedicated CUDA kernel (`LCRProjectKernel`) and Stage 2 evaluation pipeline that performs Level-Conserving Rescaling (LCR) combined with Aggregated Key-Switching (AKS). On GPU device vectors, it computes the $c_1$ AKS key-switch with `ModDownQPtoQNTT` (without dropping $q_L$) and computes $c_0$ via INTT, centered integer division by $q_L$, projection onto $q_L$, and NTT, outputting a ciphertext preserving the input modulus level.

**Blocked by:** 02: AKS Key Container and GPU Keygen Pipeline

**Status:** ready-for-agent

- [ ] `LCRProjectKernel` implemented in CUDA, supporting both 32-bit and 64-bit word types with centered integer division and modular projection
- [ ] Stage 2 evaluation operator executes the complete LCR+AKS sequence on GPU memory without host-device synchronization
- [ ] Unit test in `unittest/LCRStepTest.cpp` verifies that output ciphertext remains at the input level $L$ (no modulus level dropped) and satisfies numerical correctness bounds
