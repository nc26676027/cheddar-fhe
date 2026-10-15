# MEBOOT Discrete Identity: six-bit extractor and integration

Status: three fresh-keyset full-slot >18-bit GPU gates passed, 2026-10-15.
Full identity qualification is the
strict worst-slot test in `unittest/MEBootDiscreteIdentityTest.cpp`.
The original workspace and inherited evidence remain read-only.

## Extractor blocker resolved

The six-bit manifest is `parameters/research/discrete_direct6_d95.json`.
It contains six independently fitted degree-95 Chebyshev polynomials, with
offset-binary convention `unsigned=I+32` and argument `(z+1/2)/32`.
It is a sampled minimax fit of the paper's multi-interval construction,
not an author coefficient table or a claim of executing classical Remez.

`docs/evidence/discrete-identity/direct6-certificate.json` independently
certifies the exact binary64 coefficients over all 64 closed cells centered
at integers [-32,31], each of radius 2^-12. The largest certified digit
error is 0.000814897370 (rounded upwards), below the unchanged 2^-9 gate.
The certificate uses exact rational Taylor expansions and triangle bounds;
it excludes complex perturbations, encoding, key switching and encrypted
arithmetic. Its two-cleaning signed-integer bound is 1.31372e-10 (rounded
upwards). That conservative ideal bound alone does not prove >18 final
message bits at gap 32768.

Degree 63 was also tried with the same bounded sampled fitting method.
Its bit-0 sampled error was about 0.5 and it failed. Degree 95 has actual
compiled depth 7 in `EvalPolyBatch`; no claim of globally minimal degree
or depth is made.

## Shared cleaner and scale diagnosis

The standalone test covers every signed integer, at both radius endpoints
and zero perturbation, against independent 100-decimal-digit Clenshaw
known answers. The six-bit case uses the dense application secret HW2048.
It repeats evaluation and checks input/output aliasing and live allocation
stability, using the same cleaner implementation as the five-bit baseline.

| Schedule | Observed result |
| --- | --- |
| Input/output extraction scales 2^40/2^40 | Failed: 25.25 integer bits; final HE noise dominates |
| Input/output extraction scales 2^43/2^43 | Failed: large extraction error, before cleaning |
| Input/output extraction scales 2^40/2^43 | Passed: 37.20 integer bits, max error 6.31453e-12 |

The second schedule grows the power-basis scales faster than the 40-bit
primes remove them. The fix honors the existing `extraction_scale`
parameter, which was previously ignored: argument/power-basis input stays
at 2^40, while the polynomial returns at 2^43. Two shared cubic cleanings
then finish near scales 2^49 and 2^67. Original five-bit parameters use
2^40 for both scales, so their arithmetic schedule is unchanged.
Both failed runs are retained in explicitly named evidence files.

## Common integer and modulus ledger

The research class leaves `BootContext::Boot` dispatch unchanged. It uses
the selected `BootParameter`, `BootTransforms`, `EntryGadget`, existing
four-role `MEBootCtS`, the shared `DiscreteIntegerCleaner`, and existing
`LinearTransform`, NTT, centered ModUp, rescale and key-switch primitives.
No CUDA kernel or separate runtime was introduced.

| Stage | Level / scale | State and units |
| --- | --- | --- |
| StC and Low-q0P entry | L2 -> L0 / 2^33 | Dense -> extension-8 subring, HW40; physical StC gain 1/64 |
| Common centered lift | L0 -> L32 / 2^48 | Integer multiplier 32768, temporary q* present |
| Bypass fork | L32 -> L31 / 2^48 | Componentwise main-Q residue restriction, then sparse-to-dense switch |
| M1 | L32 -> L31 / 2^48 | Removes q*, key-free Subring Prefix |
| M2 | L31 -> L31 / 2^48 | LCR+AKS, subring -> dense |
| M3/M4 | L31 -> L29 / 2^48 | Dense BSGS, two main-Q rescale operations |
| Low branch alignment | Drop to L21, rescale to L20 / 2^40 | Explicit physical multiply/rescale after split-factor correction |
| Argument normalization | L20 -> L19 / 2^40 | `(z+1/2)/32` |
| Six-bit extraction | L19 -> L12 / 2^43 | Shared degree-95 power basis, depth 7 |
| Cleaning 1 | L12 -> L10 / about 2^49 | Two multiplications/relinearizations, two rescales |
| Cleaning 2 | L10 -> L8 / about 2^67 | Same shared cubic |
| Accurate bypass | Drop to L29, native eight factors -> L21 / 2^48 | Same lifted I; reuses BootTransforms::Bypass |
| Bypass alignment | Drop to L9, multiply/rescale -> L8 / about 2^67 | Physical gain 8/(q0/2^33), scale aligned to cleaner |
| Subtract, normalize | L8 -> L7 / 2^40 | Multiply by application-message gap, then final rescale |

The lifted integer units are `q0/2^33`, about 512. Both CtS branches use
total matrix gain `1/(32768*(q0/2^33))`. Thus their output physical units
are `I + m/g + e`, with application-message `g=q0/((1/64)*2^33)`, about
32768. Low and high branches are never independently raised.
Exact encoded M1/M2 CRT bound checks are shared with production
`MEBootExecution`, and use the exact integer q*.

## Identity gate and precision diagnosis

The first integrated full-slot run, with a bypass starting at L20, failed:
average 16.86 bits, worst 14.89 bits. Its cleaned-integer error was
8.04073e-12, while the bypass message error was 3.28813e-5. Cleaner inputs
were within 7.93e-6 of integers, below the certified radius, and the largest
observed integer magnitude was 8.

A second custom bypass started at L23 with fifteen elementary factors.
It improved average precision to 18.23 bits but still failed worst-case
precision at 16.27 bits. The oracle isolated StC/entry error 2.04632e-6
and encrypted-CtS error 1.23888e-5. Both failed logs are retained.

The current bypass reuses the production eight-factor L29->L21 transform,
then physically removes its gain and aligns to the cleaner at L9->L8.
The test separates StC/entry, post-lift and encrypted-CtS errors with a
plaintext matrix oracle.
The pass threshold remains **maximum complex slot error <2^-18** across
all 32768 slots. A median or average pass cannot substitute for it.
The first pass produced max complex error 1.99962e-6: worst 18.9318 bits,
median 20.819 bits, average 20.7254 bits. The cleaned-integer residual was
7.46481e-12; the encrypted bypass transform error in message units was
4.07321e-8; StC/entry error was 2.00414e-6. StC/entry now dominates.
Peak tracked GPU allocation was 14,548,030,360 bytes (about 13.55 GiB).
This initial pass used one keyset; repeated and independent-keyset results
are recorded separately. Its raw log is `meboot-discrete-first-pass.log`.

## Final three-keyset qualification

The final affected targets were rebuilt after adding the audit's logN16
input guard and explicit nonfinite checks for repeated and aliased outputs.
Three separate test processes each generated a fresh keyset, evaluated all
32768 complex slots, repeated the evaluation three times without observers,
and checked whole-bootstrap input/output aliasing. All gates passed:

| Fresh keyset | Average bits | Worst bits | Maximum complex error |
| --- | ---: | ---: | ---: |
| 1 | 20.7259 | 18.8975 | 2.04781e-6 |
| 2 | 20.7255 | 18.8756 | 2.07911e-6 |
| 3 | 20.7273 | 18.9616 | 1.95885e-6 |

The strict gate remained max error <2^-18. Repeated/aliased outputs retain
these bounds, and tracked live allocations do not grow between repeated
calls. The raw campaign is `meboot-discrete-tests.log`; the machine-readable
aggregate is `meboot-discrete-summary.json`, both under
`docs/evidence/discrete-identity/`. Tests use a fresh
`std::random_device`-seeded test PRNG per process, deterministic message
classes and unchanged parameter/coefficient profiles.

The final standalone cleaner run also passes all three GTests: its dense
six-bit output has 36.8608 bits (8.0129e-12 error), while the earlier fresh
keyset had 37.2045 bits. These are different keys, not a weakened threshold.

The full bounded regression run passes 15/15 CTest targets in 1050.18 s.
This includes native Ordinary/MEBOOT, both Discrete routes, centered CRT,
entry, encoding, AKS, LCR, fused/unfused prefix equivalence and transforms.
The performance GTest skips without its opt-in environment variable;
the previously timed-out `basic_test` is excluded. Raw output:
`docs/evidence/discrete-identity/regression-successor.log`.

## Pilot resource measurements and comparison limits

The three-keyset campaign supplies nine prepared online samples:
median CUDA-event time **370.738 ms**, median wall time **379.814 ms**.
Preparation, key generation, observers and decryption are outside those
timers. The same input is reused after the observed first call.
This is a solo pilot with three repetitions per keyset. Earlier regression
samples were 1053.69-1151.05 ms GPU for the same arithmetic schedule;
run-context variability remains unresolved. These data do not establish
a speedup, a stable performance envelope or a matched comparison.

Resident evaluation-key arrays total **10,497,294,336 bytes (9.7764 GiB)**.
Peak tracked GPU allocation is **14,548,030,360 bytes (13.5489 GiB)**.
The key number counts the actual rotation, conjugation, multiplication,
both secret-switch and AKS arrays. It is not a serialized-key file size.
Reserved GPU memory, transfer volume and host peak RSS are not measured.

The Discrete route returns **L7/2^40**, five levels above its L2 input.
Native MEBOOT returns L21/2^40, nineteen levels above input. This
implementation meets the requested output-precision target with a
substantial remaining-modulus tradeoff. Its accuracy, latency and modulus
capacity must be compared together.

## Domain and security scope

The exact public-domain audit is generated by
`tools/research/audit_meboot_discrete.py`. Q, P, q*, entry Q0P, secret
weights and entry sigma match the frozen Identity19 profile. Extra dense
rotations stay within the inherited unlimited-sample main-key model.
Recorded heuristic classical minima remain main 132.466948663 bits and
entry 136.379840655 bits under that model; this is exact-domain reuse,
not a new lattice-estimator run or security proof.
This audit does not qualify the separate HW30 baseline's modified chain
or secret distribution for a matched 128-bit security comparison.

A centered lift under fixed-weight ternary H40 bounds the integer
representative by |I|<=21, which lies in the certified six-bit domain.
The fractional radius after entry/CtS and complex/encrypted numerical
error remain separate gates. The previous conditional K20 Irwin-Hall
reference has log2 single tail -163.458856 and conservative union
log2 tail -145.458856 at threshold 19.4990234375. It does not include
entry noise and is not a proof of that fractional-error gate.

## Reproduction and remaining qualification

Use the explicit Python environment with numpy/scipy/mpmath available:

```powershell
$researchPython = 'C:\Users\26676\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $researchPython tools/research/generate_discrete_extractor.py --width 6 --output build/research/direct6-regenerated.json
& $researchPython tools/research/certify_discrete_extractor.py --manifest parameters/research/discrete_direct6_d95.json --output build/research/direct6-recertified.json
& $researchPython tools/research/audit_meboot_discrete.py --output docs/evidence/discrete-identity/meboot-discrete-parameter-audit.json
```

The measured machine is the RTX 5060 Ti with CUDA 12.8, Release, SM120:

```bash
cd '/mnt/d/code space/cheddar-fhe-discrete-identity/build'
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=120
cmake --build . --target discrete_integer_cleaner_test meboot_discrete_identity_test -j8
ctest -R '^(discrete_integer_cleaner_test|meboot_discrete_identity_test)$' -V
ctest -R '^meboot_discrete_identity_test$' --repeat until-fail:3 --timeout 600 -V \
  > ../docs/evidence/discrete-identity/meboot-discrete-tests.log 2>&1
```

Aggregate the three fresh-process keysets from the workspace root:

```powershell
& $researchPython tools/research/summarize_meboot_discrete.py --logs docs/evidence/discrete-identity/meboot-discrete-tests.log --output docs/evidence/discrete-identity/meboot-discrete-summary.json
```

The complete >18-bit identity gate is established separately from the
extractor certificate and standalone cleaner gate. Matched GPU timings,
broader input campaigns, full-circuit fusion ablation and complete serialized
EVK/host-memory measurements remain open.

## Requested A-N status

| Item | Current evidence or remaining work |
| --- | --- |
| A. Original reproduction | Independent HW30 baseline gate passes; latest average 14.9225/worst 11.7347 bits. No claim of the paper's 19/81-bit parity. |
| B. Reused Cheddar components | Matrix planner, BootTransforms, EntryGadget, MEBootCtS, EvalPolyBatch/BasisMap, NTT, ModUp, rescale, RNS arithmetic and native key maps. |
| C. New kernels/operators | No new CUDA kernel; new research orchestration, shared encoded-LCR audit and corrected extraction-scale binding. |
| D. Baseline precision/depth | Existing five-bit degree-47/depth-6 extractor, two cleanings, L8/2^40 output; committed initial and current regression logs retained. |
| E. MEBOOT parameters | HW40, extension 8, subring degree 8192, base-16 eleven-row entry, dense HW2048; exact Q/P unchanged. |
| F. DigitExtract | Six bits, degree 95, compiled depth 7, full radius-2^-12 cell certificate on [-32,31]. |
| G. VecClean | Two iterations, four multiplications/relinearizations and four rescale levels per channel; shared implementation. |
| H. Modulus ledger | State table above: M1 removes q*, M2 conserves level, cleaner L20->L8, final L7/2^40. |
| I. Security/domain | Exact-domain reuse audit; inherited heuristic screens only; deterministic integer coverage and separately tested fractional radius. HW30 baseline security is separate. |
| J. Identity precision | Three fresh keysets, worst 18.8756-18.9616 bits across all 32768 slots, repetitions and aliasing. |
| K. Timing/fusion | Solo nine-sample pilot recorded. Matched benchmarks and full-circuit fused/unfused ablation remain. Existing prefix equivalence regression passes. |
| L. Memory/EVK | 9.7764 GiB resident key arrays; 13.5489 GiB peak tracked allocation. Serialized EVK, reserved memory, transfers and host peak RSS remain. |
| M. Robustness/regression | 15 selected CTest targets pass; opt-in performance case skips, known basic_test timeout excluded. Three-keyset finite/repeat/alias/live-allocation checks pass. |
| N. Next work | Match precision, output state, security and workload before comparing complete routes; profile digit/bypass costs and investigate timing variability before fusion tuning. |
