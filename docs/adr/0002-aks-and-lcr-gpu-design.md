# AKS Key Management and LCR GPU Execution Design

## Context
Phase 1 of porting MEBOOT CtS requires executing M2 (LCR + AKS) on the GPU and managing the dedicated AKS evaluation keys. AKS keys require pre-encoding plaintext matrix diagonals into the automorphism evaluation keys, unlike standard Galois rotation keys. Furthermore, LCR preserves the top modulus level $q_L$ through a centered integer division and projection operation.

## Decision
1. **AKS Key Management**: We introduce a dedicated `AKSKeyMap` container, separated from the general `EvkMap`. The `UserInterface` exposes a `PrepareAKSKeys` method, allowing `MEBootCtS` to pass precomputed diagonal plaintexts and generate the required AKS keys on the GPU.
2. **LCR GPU Pipeline**: We implement a dedicated, lightweight CUDA kernel (`LCRProjectKernel`) that operates directly on device vectors. It performs coefficient-wise centered division by $q_L$ and projects the small integer back into the $q_L$ modulus ring, sandwiched between Cheddar's existing GPU-INTT and GPU-NTT handlers.
3. **M3/M4 Execution**: We convert MEBOOT's folded diagonal matrices into Cheddar's `StripedMatrix` structures and evaluate them using Cheddar's existing `LinearTransform` and double-hoisted `HoistHandler`.
4. **Verification Strategy**: We adopt a two-stage testbed approach: first validating CtS in isolation via `unittest/MEBootCtSTest.cpp` for precision and level conservation, followed by end-to-end integration into `unittest/Bootstrapping.cpp`.

## Consequences
- Clean separation between standard rotation keys and diagonal-embedded AKS keys in the API.
- Minimal kernel development overhead while retaining maximum GPU memory bandwidth and avoiding CPU-GPU data transfers during LCR.
- Maximum code reuse of Cheddar's existing double-hoisting CUDA infrastructure for M3/M4.
