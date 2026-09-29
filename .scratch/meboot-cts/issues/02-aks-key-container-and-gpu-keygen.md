# 02: AKS Key Container and GPU Keygen Pipeline

**What to build:**
A dedicated evaluation key container (`AKSKeyMap`) and GPU-accelerated key-generation routine in `UserInterface` that takes a set of diagonal plaintext polynomials and pre-encodes them directly into automorphism evaluation keys $(-a \cdot s_{\text{dense}} + \frac{P \cdot m_k \cdot \tau_k(s_{\text{sparse}})}{q_L} + e, a)$ on device memory.

**Blocked by:** 01: Matrix Planner and Golden Reference Verification

**Status:** completed

- [x] `AKSKeyMap` container implemented in Cheddar core to store and look up AKS evaluation keys by Galois element/rotation index
- [x] `UserInterface::PrepareAKSKeys` implemented to generate AKS evaluation keys directly on GPU from input diagonal plaintexts
- [x] Unit test in `unittest/AKSKeyTest.cpp` verifies successful key generation, device storage, and basic homomorphic key-switch roundtrip correctness
