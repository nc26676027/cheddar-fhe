# Cheddar FHE Context

GPU-accelerated fully homomorphic encryption library specializing in high-performance CKKS operations and bootstrapping.

## Language

### Bootstrapping Transformations

**CoeffsToSlots (CtS)**:
The homomorphic linear transformation that extracts polynomial coefficients of a ciphertext into plaintext slots.
_Avoid_: C2S, homomorphic IFFT, decryption-to-slot

**SlotsToCoeffs (StC)**:
The homomorphic linear transformation that maps plaintext slots back into ciphertext polynomial coefficients.
_Avoid_: S2C, homomorphic FFT, slot-to-poly

**MEBOOT**:
The multi-role CKKS linear transformation architecture that minimizes modulus consumption and key footprint through factor decomposition.
_Avoid_: Statistical bootstrap, meboot R package

### MEBOOT Mechanism Roles

**Level-Conserving Rescaling (LCR)**:
A specialized rescaling technique that divides the plaintext scale by a modulus while preserving the ciphertext's current modulus level.
_Avoid_: In-place rescale, zero-cost scale down

**Aggregated Key-Switching (AKS)**:
A transformation technique where plaintext matrix diagonals are directly pre-encoded into automorphism evaluation keys to eliminate separate plaintext multiplications.
_Avoid_: Fused KS, bundled automorphism

**BSGS Folding**:
A structured matrix decomposition that groups linear transform diagonals into baby-step and giant-step shifts based on remaining radix layers.
_Avoid_: Generic BSGS, baby-giant splitting

**Subring Prefix**:
An initial factor transformation evaluated over a subring using ring automorphisms to eliminate rotation key transmission.
_Avoid_: Ring switching, dimension drop

### Key and Execution Containers

**AKSKeyMap**:
A specialized container holding evaluation keys that embed plaintext matrix diagonals for aggregated key switching.
_Avoid_: Diagonal EVK map, fused key store

**LCR Projection**:
The modular arithmetic operation in LCR that maps small centered integers back into the preserved top prime limb without dropping the modulus level.
_Avoid_: Modulus lift, limb rejuvenation

**MEBootMatrixPlanner**:
The component that factors the CtS linear transformation into multi-stage diagonal matrices and calculates BSGS folding supports.
_Avoid_: FFT matrix builder, diagonal encoder
