# Discrete CKKS + MEBOOT depth and scaling follow-up

Historical L18 campaign. The subsequent
[exact affine entry correction](no-affine-rescale-20261016.md) removes its
remaining input rescale and qualifies L19 separately. Measurements below
remain the original L18 evidence.

Campaign: 2026-10-16. Base revision:
`797761434b9ce5bd0b52a976ff1036f5d75e61c6`, worktree
`cheddar-fhe-discrete-identity`, branch `codex/discrete-identity`.

The selected extension now retains the low CtS output at L29, fuses affine
normalization with scale alignment, and uses one RNS rescale after each
thrifty cubic. It returns L18 at scale 2^40, compared with L7 at the base
revision. The native `BootContext::Boot()` dispatch and public Q/P,
OverModRaise prime, entry gadget and secret distributions are unchanged.

## Measured outcome

All rows use three fresh keysets, 90 timed calls and all 32768 slots.
All return scale 2^40 and pass both the 2^-18 worst-complex-error and
2^-33 integer-residual gates.

| Configuration | Output | GPU median ms | GPU p95 ms | Host median ms | Worst slot bits | Pool peak GiB |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Preserved base revision | L7 | 343.282 | 343.720 | 338.317 | 18.8195 | 13.5567 |
| Selected retained-chain schedule | L18 | 524.597 | 525.182 | 517.885 | 18.8501 | 13.6129 |
| Low-segment latency alternative | L9 | 360.904 | 361.341 | 357.331 | 18.8770 | 13.5587 |

L18 retains eleven more levels for 52.82% more GPU latency; L9 retains two
more for 5.13% more GPU latency. The selected route's largest instrumented
costs are DigitExtract, 246.014 ms, and Cleaning, 118.381 ms, including both
channels. More upper-chain limbs make this a depth optimization with a
latency cost. Resident evaluation-key arrays remain 10497294336 bytes
(9.7764 GiB). Pool peaks exclude CUDA context and other device allocations.

See the [complete measured stage table](../../evidence/discrete-identity/depth-20261016/measurements.md)
and [validated comparison JSON with log hashes](../../evidence/discrete-identity/depth-20261016/comparison.json).
Some CUDA-event intervals exceed their host steady-clock intervals in this
WSL environment. Both clocks are reported without deriving a small speedup
from their difference.

## Arithmetic and modulus ledger

All levels refer to the existing main-Q chain; level L includes q0 through
qL. The exact prime values and scale calculations are captured in the
campaign's parameter audit JSON. The shared degree-95 extractor has depth 7.
Six extracted bits do not mean six sequential extraction levels.

The existing chain has approximate log2 prime magnitudes
`[42, 33.000088, 27.009829, 40 x 18, 58 x 4, 59 x 5, 36 x 2]`.
The last 36-bit magnitudes have small deviations recorded in the audit.
P has magnitudes `[58,58,59,59,60]`, and the temporary OverModRaise prime
has magnitude 53. These are logarithmic magnitudes, not integer
`bit_length` values; primes just above a power of two have a larger
`bit_length`.

`log2(Q2)=102.009916`, `log2(Q7)=302.009923`,
`log2(Q9)=382.009907`, `log2(Q18)=742.009870`, and
`log2(Q31)=1421.009950`. The selected output retains approximately 440
additional modulus bits. The two cleaning rescales consume about 98 bits
instead of the old four rescales' 160 bits.

| Stage | Previous route | Selected route | Selected output log2(scale) |
| --- | --- | --- | --- |
| StC, ScaleDown, dense-to-subring entry | L2 to L0 | L2 to L0 | 33 |
| Centered lift, OverModRaise | L0 to L32 | L0 to L32 | 48 |
| MEBOOT low CtS | L32 to L29 | L32 to L29 | 48 |
| SplitRealImag, restore integer units | L29 | L29 | 49 |
| Restrict cleaner basis | L29 to L21, 8 drops | L29, no drops | 49 |
| Scale alignment | L21 to L20 | fused affine, L29 to L28 | 59 |
| Normalize `(I+0.5)/32` | L20 to L19 | included in preceding operation | 59 |
| Six digit polynomials, shared basis | L19 to L12 | L28 to L21 | 32 |
| Cubic cleaning 1 | L12 to L10, 2 rescales | L21 to L20, 1 rescale | approximately 38 |
| Cubic cleaning 2 | L10 to L8, 2 rescales | L20 to L19, 1 rescale | approximately 74 |
| Combine signed integer | L8 | L19 | approximately 74 |
| Subtract accurate bypass, normalize | L8 to L7 | L19 to L18 | 40 |

On the integer branch after low CtS, this removes eight restriction drops,
one duplicate affine rescale and two cleaning rescales: 11 recovered main-Q
levels. L29 to L18 consumes 11 rescale levels, compared with eight drops
and fourteen rescale levels for L29 to L7. L18 leaves 16 levels above the
L2 application input. Native Identity19 still returns L21.

The accurate branch still uses the existing native bypass at L29 to L21.
It restricts to L20 and physically aligns to the cleaner at L19, then the
shared subtraction/normalization returns L18. Both branches originate from
the same lift and retain the same integer representative.

The power-basis scale 2^59 belongs to the upper 58/59-bit prime segment.
It is distinct from the digit-output scale 2^32. Keeping the original
power-basis scale 2^40 while simply deleting the eight drops is not the
implemented optimization.

## Cleaning and precision constraints

Each iteration already used the paper's thrifty algebra:

```
square = Relin(x*x)
cube   = Relin(x*square)
output = Rescale(3*D*square - 2*cube)
```

Two ciphertext multiplications remain necessary per cubic; the improvement
is modulus consumption. The prepared schedule now records each iteration's
rescale count and derives all downstream levels from it. Exact integer
encoding of 3D, nearest-integer scale reinterpretation and its error ledger
are retained. The original non-normalized cleaner entry remains available.

The backend divides by actual RNS primes. Selected cleaning scales follow
`D1 = D0^3/q21`, then `D2 = round(D1)^3/q20`, approximately 2^38 and
2^74. This is not an implementation of arbitrary `Rescale_D` or Grafting.
The final encoded normalization integer must remain at least 2^20; its
actual value is included in the audit. Omitting rescale operations without
retuning extraction and normalization does not satisfy this contract.

The complete identity gate remains maximum complex slot error below 2^-18,
and integer residual below 2^-33. Averages never replace either gate.
The known-answer test separately covers all 64 signed centers and their
fixture perturbations, checks each extracted/cleaned digit against its
independent oracle, and exercises reuse and aliasing.

The extraction-scale sweep is conditional on the rescale schedule. For the
one/two-rescale schedule, 2^33 and 2^34 fail while 2^35 passes a complete
identity trial. For the one/one-rescale schedule, 2^32 passes the complete
identity trial and all-cell test. All-cell trials at 2^30 and 2^31 also
pass, but do not establish a full-slot, multi-keyset minimum. The selected
2^32 retains extra cleaning precision. Low-segment all-cell trials at
2^24 and 2^25 fail; 2^26 passes with the one/one-rescale schedule.

## Profiling and reproducibility

The campaign directory is
`docs/evidence/discrete-identity/depth-20261016/`.
Its summaries, raw logs, source/binary hashes and parameter audits are the
measurement record. Headline latency uses unobserved complete `Evaluate`
calls with prepared keys; key generation, setup and decryption are excluded.
Every timed result is decrypted and checked outside the timers.

Each selected configuration uses three fresh processes/keysets, five
warmups, thirty timed calls and five instrumented stage profiles per
process. The old route uses matching three-process/five-warmup/thirty-call
measurements with its preserved library and matching old headers. Its
single stage profile precedes the five warmups; that instrumentation is
not used as headline latency.

GPU work is serialized. Stage profiles use CUDA events without decrypting
or synchronizing at each boundary. Digit extraction, each cleaning round
for each bit, and recombination are now observable. Instrumented totals
are reported separately from unobserved calls. Host wall intervals for
asynchronous stages measure submission and are not GPU stage durations.

The latency alternative uses the same extension with alignment at L21,
power basis 2^40 and extraction scale 2^29, using the one/two cleaning
schedule and returning L9. It intentionally restricts the upper chain
to reduce RNS arithmetic. Such a restriction is a configurable latency
tradeoff, not an algorithmic requirement.

```
python3 tools/research/profile_discrete_depth.py \
  --out docs/evidence/discrete-identity/NEW-CAMPAIGN \
  --name depth18

python3 tools/research/profile_discrete_depth.py \
  --out docs/evidence/discrete-identity/NEW-CAMPAIGN \
  --name latency9 --alignment 21 --basis 40 --extraction 29 --rescales 12
```

The selected API call remains
`MEBOOTDiscreteIdentity(context, profile, coefficients)`.
To select the latency configuration, build `DiscreteIdentityParameters`
with `FromCoefficients`, set `cleaner_alignment_level=21`,
`input_scale=2^40`, `extraction_scale=2^29`, `cleaning_rescales={1,2}`, and
pass that object to the existing parameter overload.

Two infrastructure trials are excluded from qualification: the first old
harness used mismatched headers/library and faulted during destruction;
one low-scale cell trial attempted to load the library during relinking.
Both raw failures are retained. Their corrected isolated/stable reruns,
rather than those incomplete runs, provide the accepted evidence.

The L10 low-segment candidate at extraction scale 2^26 is rejected:
although its 256-slot all-cell test passed narrowly, the full 32768-slot
identity had integer residual 6.3025e-10 and maximum message error
2.06927e-5. This is why cell tests and complete identity qualification are
separate gates.

The inherited coefficient certificate references CRLF manifest bytes while
the current checkout contains LF bytes. Converting LF to CRLF reproduces
the inherited hash exactly. The campaign reruns the exact-rational checker
against current bytes and records a new certificate; neither the frozen
coefficient data nor inherited certificate is rewritten.

The two configurations return different levels, and are compared for this
same fixed message grid and inherited public parameter domain. This does
not establish a speedup at equal output level against native Identity19,
the original HW30 route, or the paper's higher-precision configurations.

## Final verification

- Nine fresh-process identity cases and 270 timed calls qualify the
  L7/L18/L9 comparison. Repeated calls, aliasing, finite outputs and
  stable live allocations pass.
- All four cleaner cases pass, including original five/six-bit behavior
  and the new normalized upper-chain schedule. The L9 alternative's
  separate all-cell test also passes.
- Thirteen inherited regression targets are verified: twelve pass in the
  first serialized run; the native complete target times out at the
  initial 240-second limit and then passes all nine cases in a full retry,
  taking 281.92 seconds with an effective 480-second allowance.
- The preexisting `basic_test` timeout is excluded. The opt-in native
  performance case skips; this is not counted as a matched native
  performance measurement.
- Both parameter audits, fresh exact-rational certification, runtime
  fixture equality, source/binary/log bindings and `git diff --check` pass.
- Changes remain uncommitted in the dedicated worktree. The original
  workspace and historical evidence are preserved.
