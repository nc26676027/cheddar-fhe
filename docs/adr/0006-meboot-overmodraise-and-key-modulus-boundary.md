# Separate OverModRaise from the normal key-switch modulus chain

Accepted on 2026-10-13 following the owner's correction of the Phase 1 depth
accounting. This supersedes ADR-0005's one-level depth policy and the previous
word-specific starting-level policy; ADR-0004's supported input contract remains.

## Problem

Standard CtS begins with key switching and must stay inside its normal Q/P
envelope. A key-free prefix may use a larger modulus only until its rescale
returns to that envelope. Counting every numeric level equally hid this
distinction. Cheddar's existing terminal primes are ordinary mixed-base chain
components, not inherently OverModRaise primes. The former 32-bit comparison
changed the starting level and produced the same output depth; the 64-bit
prefix consumed a normal-chain prime.

## Decision

Use the same security-screened normal Q/P envelope for the paired standard and
MEBOOT paths. Add one distinct temporary prefix level above it for MEBOOT:
two 30-bit primes for 32-bit words, one 55-bit prime for 64-bit words.
Parameter::max_key_switch_level_ marks the normal ceiling independently of
max_level_. Existing profiles without a prefix default to the previous ceiling.

Standard BootContext starts at the normal ceiling. MEBOOT starts one level
above it: M1 removes only the temporary modulus, M2 LCR+AKS preserves the normal
ceiling, and M3/M4 consume two normal levels. Standard CtS consumes four normal
levels. Both paths use the same normal Q/P envelope, and MEBOOT retains two
extra output levels after the shared eleven-level EvalMod and three-level StC.

Ordinary encryption, rotation-key generation, basic evaluation keys and AKS
keys exclude temporary prefix primes. Requests for keys above the normal
ceiling are rejected. The deterministic testing-only contract constructor
remains an isolated-operator tool, not a secure encryption interface.

ModUp uses the requested BootContext basis directly, so standard StD never
touches the temporary prefix. KS decomposition padding uses the normal-key
terminal origin; secret/primes/P-product storage retain the full array origin.
Existing ModUp, Rescale, NTT, RMM and LinearTransform kernels are reused.

The sparse entry is screened at its actual effective ring degree N/16, rather
than using a full-ring estimate. The former h=32 entry did not meet the
128-bit screen. The aligned subring preset uses h=256 and DtS-only error
standard deviation 819.2; ordinary encryption, dense keys and AKS retain 3.2.
Both paired paths share the same subring entry key and input ciphertext.

Increasing the sparse weight also requires K=64 for modular reduction.
The shared EvalMod uses a degree-46 Chebyshev cosine coefficient vector
(negligible terms are pruned), six polynomial levels and five double-angle
levels. Both paths solve the scale recurrence backward for a bounded end scale.
BootContext and EvalMod share the calculation. The legacy K=16 preset retains
its original discrete-interval polynomial and standard scale initialization.

In the 32-bit profile, 60-bit intermediate scales gave approximately 122 dB
after key-switch and rescale rounding was amplified by the expanded EvalMod.
A plaintext-only ciphertext isolated that error from polynomial approximation.
The aligned chain allocates approximately 71--73 bits per upper rescale using
three small NTT primes, and approximately 54 bits per StC rescale. Normal
log2(QP) is 1743.312140, below the previous 1745.4157 envelope. This
reallocation leaves standard output level 2 and MEBOOT level 4. The 64-bit
profile uses approximately 60-bit upper rescales, log2(QP)=1657.000144,
and output levels 4 and 6. Depth comparisons must include the eleven-level
EvalMod cost, rather than quoting the former residual application depth.

The 32-bit LCR diagonal gain follows the actual top-prime bit size so that
the prime times the gain is at least 2^46. This is an encoding-scale budget;
actual precision is qualified by the paired bootstrap tests. The dense factors
compensate that gain, preserving the full CtS matrix. This avoids tying
precision to one hard-coded modulus profile. Matrix-only golden fixtures
with unit scaling retain the original factor matrices.

## Consequences

Normal terminal primes remain part of the security-critical Q/P envelope.
Excluding only the added prefix fixes modulus-budget exposure; it is not a
standalone security-bit certificate. Any cryptographic estimate must use the
actual dense or subring secret distribution, error distribution, sample model
and published-key modulus. See the primary-source research and validation
reports for the measured boundary and remaining security assumptions.

Historical validation reports remain dated snapshots. Their previous one-level
claims must not be used as the current main-Q accounting.

## Validation

Accepted on 2026-10-13 on `feat/meboot-cts` after commit 033fe21. The frozen
serial regression passed 11/11 targets, extra Basic and StC checks passed, and
the 30-sample paired bootstrap reproduced 134.03/134.03 dB (32-bit) and
124.90/125.03 dB (64-bit) with MEBOOT median latency 1.47%/2.42% below the
standard path. The modulus audit pins output levels 2/4 and 4/6. Security
evidence remains a heuristic screen: entry minima 129.75/130.95 and dense
five-attack minima 130.41/137.88; dense MITM is permanently INCOMPLETE and
`dual_hybrid = oo` is not a bound. The bounded-CRT check is a floating-point
diagnostic (32-bit margin 0.45 bit), not a certified interval proof. Details
and remaining theorem-level items are in
`docs/research/meboot-modulus-security-20261013.md`.
