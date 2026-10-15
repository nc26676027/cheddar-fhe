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
then rescale once. The current moderate schedule also rescales once to
reduce the intermediate scale before the next iteration. Thus each iteration
consumes two main-chain levels. The exact `3*D`
constant is formed directly as RNS residues rather than a rounded double
product. Current moderate implementation requires integer `D<2^63`; the
opt-in binary128 preparation helper is a separate extension.

`(z+1/2)/2^(width-1)` deliberately changes the represented units: add the
exact half-offset, multiply by an encoded reciprocal, then rescale once.
This consumes one main-Q prime and preserves the argument's declared CKKS
scale. The input, argument and output units are named explicitly in code
and tests. `extraction_scale` separately sets the polynomial output scale:
it need not equal the argument/power-basis input scale.

An observer can inspect extracted bits, each cleaning iteration and final
recombination in correctness runs. It is absent during online measurements.
Reference generation and decryption never execute inside the GPU cleaner.
The implementation stays on Cheddar's legacy stream and existing RMM pool;
parallel digit streams remain unqualified until a serial correctness gate.

## Independent reference and coefficients

The exact offset-binary convention is `u=I+2^(width-1)` and
`I=sum 2^j*bit_j-2^(width-1)`. Five-bit inputs cover all 32 centers
`[-16,15]`; six-bit inputs cover all 64 centers `[-32,31]`. Both declared
noise radii are `2^-12`. The five-bit degree is 47 and the six-bit degree
is 95, with actual compiled depths 6 and 7 respectively.

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

## Implemented identity seam

The original baseline composes the existing matrix planner and
LinearTransform with StC, one centered raise and fork, a low-precision CtS,
the shared cleaner, a high-precision CtS bypass, aligned subtraction and
output normalization. Both branches must share the same lifted residues.
SplitRealImag in Cheddar returns twice the real/imaginary components;
the research graph must account for that factor in its physical units.

The baseline passed its independent GPU gate in d06720c (average 14.92,
worst 11.93 bits). This is below the paper's reported 19-bit comparator.

`MEBOOTDiscreteIdentity` uses the selected profile's StC and Low-q0P entry,
then one centered lift with multiplier 32768 at L32/scale 2^48. Before M1,
the bypass is restricted to main-Q L31 and switched to the dense secret.
M1 removes q*, M2 conserves L31, and M3/M4 return L29. The low CtS gain is
`1/(32768*(q0/2^33))`, so both branches represent `I+m/g`, where the gap
in application-message units is `g=q0/((1/64)*2^33)`, about 32768.

After SplitRealImag's factor of two is accounted for, an explicit
multiply/rescale at L21 aligns the cleaner input to L20/scale 2^40.
The shared cleaner extracts at scale 2^43, then cleans twice, ending at
L8/scale about 2^67. The bypass reuses the selected
`BootTransforms::Bypass` at L29->L21/scale 2^48. Its physical gain of
`(q0/2^33)/8` is removed by an explicit constant multiply/rescale at
L9->L8, simultaneously aligning to the cleaner output scale. Final
subtraction and physical normalization return L7/scale 2^40.

Two custom bypass schedules failed the >18-bit worst-slot gate. The native
bypass with explicit output alignment then passed all 32768 slots at
18.93 worst-case bits, average 20.73 bits, max complex error 1.99962e-6.
Three subsequent fresh keysets pass at worst 18.8756-18.9616 bits, including
repeated evaluation, finite outputs, input/output aliasing and stable live
allocations. All 15 selected regression targets pass (the opt-in performance
case skips; the known basic_test timeout is excluded). This is a
moderate-precision result, not the paper's 81-bit row.

High precision needs accurate transforms, coefficients, scale tracking and
encoding/decoding together. A binary128 reference helper alone will not
qualify the paper's 81-bit end-to-end row.
