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

**EvalRound**:
A refresh construction that combines an integer-lift correction from a
modular-reduction branch with a high-precision bypass return.
_Avoid_: Using EvalRound as a synonym for EvalMod

**Discrete EvalRound**:
An EvalRound route that removes the lifted integer with encrypted digit
extraction and integer cleaning before subtracting it from the accurate
bypass. Ordinary and MEBOOT are selectable CtS adapters for this route.
_Avoid_: Treating the discrete integer cleaner as part of MEBOOT M2

**Discrete MEBOOT route**:
The `BootContext` route selected by `BootRoute::kDiscreteMEBoot`. It reuses
the native MEBOOT entry, low CtS, accurate bypass and key preparation while
placing DigitExtract and integer cleaning behind the shared bootstrap
execution seam.

**Accurate Bypass**:
The high-precision EvalRound branch that carries the refreshed value, aligned
to the output level and scale, from which the cleaned lifted integer is
subtracted.
_Avoid_: high-cts, Accurate bypass and alignment

**DigitExtract**:
The encrypted step of Discrete EvalRound that decomposes the lifted integer
component into fixed-width digits, all evaluated over one shared polynomial
basis.
_Avoid_: EvalMod, digit decomposition

**Integer Cleaning**:
The iterative polynomial refinement that drives each extracted digit toward its
exact integer value before the digits are recombined into the lifted integer.
_Avoid_: Treating Integer Cleaning as part of DigitExtract or MEBOOT M2

**Scaling Binding**:
The contract relating a transformation's physical value gain, ciphertext
scale and modulus consumption to the units expected by the next operation.
_Avoid_: Scale metadata assignment, implicit gain normalization

**Identity19**:
The selected full-slot MEBOOT identity refresh at ring log degree 16: a level-2
input at scale 2^40 returns at level 21 and scale 2^40, giving 19 levels above
the input. Its joint entry uses HW40, extension 8, 42-bit q0 and Low-q0P;
EvalMod uses K20, actual degree 62 and two double-angle steps.
_Avoid_: Using the former Identity16-18 label for the current joint contract

**Ordinary route**:
The retained selected comparison route, with K16, actual degree 30, three
double-angle steps and level-20 output at scale 2^40.
_Avoid_: Treating Ordinary as a classic MEBOOT Phase 1 bootstrap

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
An initial factor transformation whose automorphisms preserve a subring-supported secret, eliminating rotation key transmission. A Subring Prefix can retain full-ring ciphertext representation; it does not itself imply a ring dimension drop.
_Avoid_: Ring switching, dimension drop

**Low-q0P Entry Gadget**:
The MEBOOT reference's dense-to-subring entry key switch, whose entry modulus pairs the first main-chain prime with a single auxiliary entry prime.
_Avoid_: Low-P entry, auxiliary entry key, custom-P entry

**Main-Q Chain**:
The normal ciphertext modulus chain below the key-switching modulus ceiling.
Every rescale within this chain consumes application depth, including rescaling
that changes a mixed-prime basis.
_Avoid_: Equating main-Q depth with the number of primes named main in storage

**OverModRaise**:
Modulus raising above the normal Q ceiling for operations that need no key
switching. Its temporary prefix modulus is removed before any evaluation-key
operation and is excluded from main-Q depth consumption.
_Avoid_: Classifying every terminal prime as a temporary modulus

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

**Dense-to-Sparse (DtS) Entry Key**:
The evaluation key for the entry key switch from the dense application secret to the subring-supported secret under a Subring Prefix.
_Avoid_: SSE key, short-basis key, subring key

**Entry Q0P**:
The public modulus product formed by an entry key's base primes and its auxiliary primes, over which entry-key security is screened.
_Avoid_: Entry modulus, entry QP, DtS modulus

**Domain Escape**:
The event that the lifted integer component falls outside the integer cells
covered by EvalMod's approximation domain.
_Avoid_: Equating domain escape probability with lattice attack cost or approximation error

**Subring Secret Domain**:
The effective ring dimension, secret distribution and public modulus exposed
by a key targeting a subring-supported secret.
_Avoid_: Reusing the full-ring dimension or another profile's sparse weight

### CNN Layers and Inference Pipeline

**ResidualBlock**:
A deep homomorphic neural network module encapsulating two 3x3 convolution layers,
activation refreshes, residual shortcut alignment, and optional stride-2 spatial
downsampling (`SplitAndQuadruple` and Option-A shortcut refresh) behind a uniform
`Forward(out, in, keys)` interface.
_Avoid_: Exposing spatial split transitions or manual shortcut branches to the classifier

**Fused Conv-StC**:
The linear operator fusing 2D spatial convolution weight diagonals directly into
the Slots-to-Coeffs transformation Factor 1, mapping Level 2 ciphertexts to Level 0
at scale 2^31 with 1/15 gain.
_Avoid_: Separate ciphertext-plaintext convolution multiplication

**SplitAndQuadruple**:
The homomorphic spatial quadrant folding transform remapping 2D channels on an
$N \times N$ grid into $2 \times 2$ virtual quadrant sub-channels on an $(N/2) \times (N/2)$
halved grid.
_Avoid_: Decryption-based downsampling, separate spatial striding without CinS alignment
