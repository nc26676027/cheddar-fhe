# Exact affine entry without rescaling

Follow-up to `depth-scaling-20261016.md`, in the same dedicated worktree.
The earlier campaign is retained as historical evidence.

## Corrected arithmetic

The earlier L18 schedule unnecessarily rescaled the affine input.
After splitting channels and restoring integer units, the input has scale
`S = 2^49`. Encode `1/32` at constant scale `2^10`. Its encoded integer
is exactly 32. Multiplication changes the ciphertext scale to `2^59`
and its decoded value to `(I+0.5)/32`, without changing the modulus.
The half offset is added before multiplication.

This is a real encoded-constant multiplication with the scale propagated by
`Context::Mult`. It does not relabel an unchanged ciphertext as though its
decoded value were unchanged. The implementation uses this path when
`input_scale / (2^49 * 32)` is a positive integer. Other scale contracts
retain the existing rescale path.

| Operation | Input level | Output level | Output log2(scale) |
| --- | ---: | ---: | ---: |
| Low CtS | 32 | 29 | 48 |
| Split, restore integer units, exact affine map | 29 | 29 | 59 |
| Six parallel degree-95 digit polynomials | 29 | 22 | 32 |
| Thrifty cubic 1 | 22 | 21 | approximately 38 |
| Thrifty cubic 2 | 21 | 20 | approximately 56 |
| Integer recombination | 20 | 20 | approximately 56 |
| Subtract accurate bypass, bind application output scale | 20 | 19 | 40 |

The bypass starts with the native L21 result and aligns to the integer
branch at L20. Its one rescale is on a parallel path; it is not added to
the integer branch's sequential depth. On that branch, L29 to L19
consumes seven extraction rescales, two cleaning rescales and one final
output rescale.

## What six bits say about depth

The six outputs are evaluated in parallel from a shared basis. The frozen
profile contains actual nonzero degree-95 terms. `EvalPolyBatch` compiles
it with `Log2Ceil(96) = 7` levels. The five-bit degree-47 profile uses
`Log2Ceil(48) = 6`.

For a polynomial arithmetic circuit starting from a degree-one input,
additions and scalar multiplications do not increase degree. At ciphertext
multiplication depth d, degree is at most `2^d`. Consequently six
multiplication layers cannot evaluate this exact degree-95 polynomial:
`95 > 64`. This is a bound for the current polynomial, not a proof that
every six-bit extraction approximation needs degree 95 or seven modulus
levels. A different degree-at-most-64 interval approximation would need
its own certificate and encrypted qualification. Thrifty arithmetic also
means multiplication depth and the number of rescaled primes are distinct.

## Remaining output conversion

The numerical affine map and multiplication by `gap = 32768` remain
necessary to express the existing coefficients and output in their intended
units. Their mathematical necessity does not imply a rescale is necessary.
The entry conversion above needs none.

The final implementation still binds the result to the existing exact
application scale `2^40`. After cleaning, the natural scale for the
gap-multiplied difference would instead be `cleaner_scale / gap`, about
`2^41`. Returning that natural scale could avoid the final rescale, but
would change the public scale contract and the next bootstrap's input
assumptions. No general lower-bound claim is made for this final rescale.
It should be reconsidered together with a caller-supported output scale
or fusion into a suitably validated bypass/cleaning schedule.

## Evidence

Raw logs and public-parameter audits are in
`../../evidence/discrete-identity/no-affine-rescale-20261016/`.

The no-affine-rescale pilot passes all 32768 slots, aliasing and repeated
execution, with L19 output, worst error `2.19053e-6` (18.8003 bits) and
integer residual `1.59164e-11`. Three timed calls have GPU median
572.708 ms. These are pilot numbers, not the multi-keyset qualification.

An independent full-slot pilot with basis scale `2^58` fails: integer
residual `2.92963e57`, maximum message error `9.5998e61`. Lowering the
basis scale by one bit repeatedly doubles the scale deficit while building
`T2,T4,...,T64`; it is not an innocuous reduction of extraction output
precision. The selected digit-output scale remains `2^32`, separately from
the `2^59` basis scale. This rejected candidate does not establish a
global minimum basis scale.
