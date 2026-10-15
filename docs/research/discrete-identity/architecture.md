# Shared Discrete Identity implementation

This research branch preserves ADR-0008 and the existing BootContext route.
The existing Identity19 and Ordinary source snapshot is its regression base.
No research choice changes Boot() dispatch.

## Implemented arithmetic seam

`DiscreteIntegerCleaner` consumes real slots in normalized integer units
`I+epsilon`. Its declared secret domain, basis, level, scale, signed width,
coefficient profile and input-error radius are part of the interface.
Its output records the resulting level, scale, width, extractor depth,
cleaner multiplicative depth and actual rescale count. The ideal polynomial
error bound is explicitly separate from HE evaluation noise.

`EvalPolyBatch` reuses `EvalPolyNode`, `BasisMap` and
`MultiLevelCiphertext`. All output polynomials share one basis evaluation.
The odd part of each extractor is compiled separately from its exact
one-half output offset. Existing fused polynomial-node accumulation and
relinearize/rescale remain in use. No new tensor, NTT, basis conversion,
key-switch, rescale or CUDA kernel is introduced.

Each bit cleaner follows CCS 2025 Fig. 2 right: tensor/relinearize the square,
tensor/relinearize the cube, form `3*D*square-2*cube` at physical scale `D^3`,
then rescale once. Thus two dependent multiplications consume one configured
rescale, before optional precision-budget rescaling. The exact `3*D`
constant is formed directly as RNS residues rather than a rounded double
product. Current moderate implementation requires integer `D<2^63`; the
opt-in binary128 preparation helper is a separate extension.

`(z+1/2)/2^(width-1)` deliberately changes the represented units: add the
exact half-offset and multiply the scale by the power-of-two divisor.
This map does not divide ciphertext residues or consume a main-Q prime.
It is not a value-preserving scale adjustment. The input, argument and
output units are named explicitly in code and tests.

An observer can inspect extracted bits, each cleaning iteration and final
recombination in correctness runs. It is absent during online measurements.
Reference generation and decryption never execute inside the GPU cleaner.
The implementation stays on Cheddar's legacy stream and existing RMM pool;
parallel digit streams remain unqualified until a serial correctness gate.

## Independent reference and coefficients

The exact offset-binary convention is `u=I+2^(width-1)` and
`I=sum 2^j*bit_j-2^(width-1)`. Five-bit inputs cover all 32 centers
`[-16,15]`; the declared noise radius is `2^-12`.

`generate_discrete_extractor.py` fits the paper's permitted multi-interval
direct-polynomial construction with sampled minimax linear programming.
It is independently generated, not the authors' unpublished coefficient set.
`discrete_identity_reference.py` evaluates it with 100-decimal-digit Clenshaw
and computes digits independently by integer arithmetic.
`certify_discrete_extractor.py` then bounds the full interval around every
center with exact rational Taylor coefficients. Coefficients are interpreted
as the exact binary64 values consumed by the GPU polynomial compiler.
No sampled-grid result is substituted for this continuum polynomial bound.

The polynomial certificate excludes encrypted arithmetic and key-switch
noise. The encrypted test separately compares every stage to independent
known answers, and checks all integer centers with endpoint and zero noise.
Repeated execution and result/input aliasing are separate checks.

## Pending identity seam

The original baseline will compose the existing matrix planner and
LinearTransform with StC, one centered raise and fork, a low-precision CtS,
the shared cleaner, a high-precision CtS bypass, aligned subtraction and
output normalization. Both branches must share the same lifted residues.
SplitRealImag in Cheddar returns twice the real/imaginary components;
the research graph must account for that factor in its physical units.

The later MEBOOT adapter may vary only entry and low-precision CtS at this
seam. M1/M2/M3/M4, q* removal and Low-q0P are not enabled before the complete
original encrypted baseline gate. The shared extractor, cleaning,
recombination and reference must remain the same for equal mathematical
profiles. The six-bit HW40 candidate will need a separately certified
coefficient profile and an actual graph-derived depth.

High precision needs accurate transforms, coefficients, scale tracking and
encoding/decoding together. A binary128 reference helper alone will not
qualify the paper's 81-bit end-to-end row.
