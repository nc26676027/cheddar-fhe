# Lattigo Remez six-level digit extraction

This supersedes the candidate-selection conclusion of
`restricted-domain-degree-search-20261016.md`. The sampled-LP failures remain
historical results for those candidates, not lower bounds on polynomial degree.

## Construction and certificate

The unchanged Low-q0P entry uses subring dimension 8192 and secret weight 40.
The conservative deterministic runtime domain used here is `I=-21..21`.
Offset-binary digit polynomials satisfy `P(-x)=1-P(x)` under the involution
`I -> -I-1`. Include the additional mirror cell `I=-22` and use

```text
x = (2 I + 1) / 43
|integer input error| <= 1/4096
|argument error| <= 1/88064
```

The 44 disconnected intervals are passed to Lattigo
`utils/bignum.NewRemez`, using its Chebyshev basis, 256-bit arithmetic,
maximum 120 iterations and relative convergence threshold `1e-12`.
The source is the vendored `github.com/tuneinsight/lattigo/v6` module in the
neighboring `paper1-linear-bootstrap` repository, whose parent snapshot is
`6330f64b178b6215edd35baf5b11c8da558ca95c`. That is the parent repository's
revision, not an upstream Lattigo revision. The Remez core file SHA256 is
`c74c313f7b72fa66143dfc8fb0329961366fb5a48fbc0e94597516446744d0a9`.
It was compared with upstream Lattigo v6.1.1: the only differences in this
core file are two nonfunctional `nosec` comments. The upstream file digest
and comparison are recorded in `lattigo-source-provenance.json`.
Its interval-normalized Chebyshev coefficients are converted back to the raw
Chebyshev coordinate used by Cheddar, then exported to binary64.
Even coefficients are projected to zero and the constant term is set to 0.5.
Certification applies to these final exported coefficients.

| Bit | Lattigo reported error | Exact continuum upper bound |
|---|---:|---:|
| 0 | 0.000242470976 | 0.000916728238 |
| 1 | 0.000191747608 | 0.000191821152 |
| 2 | 0.000175291452 | 0.000543843971 |
| 3 | 0.000170510003 | 0.000594192627 |
| 4 | 0.000168613931 | 0.000589610822 |
| 5 | 0.000168028703 | 0.000605221755 |

All six degree-63 polynomials pass the `1/512` extraction contract. The
certificate uses exact rational Taylor recurrences and a triangle bound over
each complete input interval. It is deliberately more conservative than
Remez's reported error and does not certify encrypted arithmetic noise.
An independent 100-digit Clenshaw scan at 2860 points observes maximum digit
error `0.000242470975937` and two-round recombination error `1.04623e-12`.

The degree-47 lowest-bit Remez probe converges to reported error
`0.499758424331` and fails the contract. With the complement symmetry above,
projecting a degree-48 polynomial to its half-offset odd part cannot increase
its worst error on this symmetric domain, and its degree becomes at most 47.
The probe therefore provides no acceptable degree-48 candidate. This report
does not claim a formally certified global minimax lower bound.

The successful degree-63 bit-0 coefficient magnitude reaches approximately
2111.77, exceeding the earlier LP's coefficient bound of 1024. Coordinate
normalization and the approximation procedure also differ. The earlier
bounded LP search did not justify requiring degree 95.

## Runtime scale binding and modulus use

`FromHW40RemezCoefficients` accepts the manifest's affine encoding fields and
checks the supported HW40 contract. The test build checks that the source
manifest SHA256 matches a passing continuum certificate. This factory accepts
caller-supplied coefficients; the attached certificate and qualification
evidence remain part of selecting a profile.

The entry argument gain `2/43` is encoded at scale 1032 as the exact integer 48.
After restoring integer units at scale `2^49`, multiplication changes the
scale to `2^49 * 1032` (log2 approximately 59.0112) without a rescale.
This is separate from the digit-output scale, which remains `2^32`.
The selected shared-basis baby step is 16. Degree 63 otherwise changes the
generic automatic choice to 8; the larger value 32 was also tested and did
not provide a reliable latency improvement. These decompositions preserve the
same coefficients and six-level output contract. Lower polynomial degree and
fewer levels do not alone guarantee lower GPU latency.

| Operation | Input level | Output level | Output log2(scale) |
|---|---:|---:|---:|
| Low CtS | 32 | 29 | 48 |
| Split and exact affine argument map | 29 | 29 | 59.0112 |
| Six parallel degree-63 digit polynomials | 29 | 23 | 32 |
| Thrifty cubic 1 | 23 | 22 | approximately 38 |
| Thrifty cubic 2 | 22 | 21 | approximately 56 |
| Signed integer recombination | 21 | 21 | approximately 56 |
| Subtract accurate bypass and bind application output | 21 | 20 | 40 |

DigitExtract consumes `q[29]..q[24]`; the cubics consume `q[23]` and `q[22]`;
the final application-scale conversion consumes `q[21]`.
From the low CtS output this is six plus two plus one sequential rescales,
instead of seven plus two plus one. The returned ciphertext retains
`q[0]..q[20]`, one more application prime than the degree-95 L19 route.
The frozen prime groups have approximate log2 sizes:
`q[0]:42`, `q[1]:33`, `q[2]:27`, `q[3..20]:40`,
`q[21..24]:58`, `q[25..29]:59`, `q[30..31]:36`.
Exact values are retained in `parameter-audit-final.json`. The retained output
modulus grows from approximately 782.01 bits at L19 to 822.01 bits at L20.
The separate approximately 53-bit OverModRaise prime is temporary and is
excluded from the main-Q depth count.

The accurate bypass natively finishes at L21. Its unit conversion and scale
binding are fused into its last existing linear factor, so it meets the
cleaned integer at L21 without an extra alignment rescale. The shared
execution checks the resulting level and scale before subtraction.
The separate final output rescale is retained for the exact public `2^40`
output-scale contract; no lower-bound claim is made about alternative
output contracts or differently scheduled cleaning.

## Performance and evaluation-tree work

On RTX 5060 Ti with CUDA 12.8 under WSL, three fresh keysets per configuration
use five warmups, thirty timed calls and five separate instrumented profiles.
Encoding, key generation and decryption are outside the timers.

| Profile | Output | GPU median | GPU p95 | Worst slot precision | Allocator peak |
|---|---|---:|---:|---:|---:|
| Degree 95, automatic baby step 16 | L19 | 574.786 ms | 590.096 ms | 18.9118 bits | 13.6417 GiB |
| Degree 63, automatic baby step 8 | L20 | 708.332 ms | 722.078 ms | 18.7777 bits | 13.5694 GiB |
| Degree 63, selected baby step 16 | L20 | 658.013 ms | 684.842 ms | 18.7960 bits | 13.6251 GiB |

The selected profile saves one output level but is **14.48% slower** than the
recorded degree-95 comparison. Its largest end-to-end integer residual is
`1.60197e-11`; average slot precision is 20.7136--20.7143 bits. Resident
evaluation-key arrays remain 10497294336 bytes (9.7764 GiB).
Allocator peaks do not measure total driver/device memory consumption.
The initial comparison and final selected binary hashes are recorded
separately; the generic legacy decomposition is unchanged.

An adjacent final-build confirmation uses one additional fresh keyset and
thirty checked calls per route:

| Final-build profile | GPU median | GPU p95 | Host median | Worst slot precision |
|---|---:|---:|---:|---:|
| Degree 95 | 544.238 ms | 544.454 ms | 546.409 ms | 18.9670 bits |
| Remez degree 63, baby 16 | 620.154 ms | 620.611 ms | 622.068 ms | 18.9418 bits |

The adjacent pair measures **13.95% higher GPU latency** for the selected
profile. Both routes ran faster during final confirmation than in the earlier
three-keyset campaign; the raw results preserve this platform variation.
Use this adjacent same-binary pair for the final latency comparison and the
three-keyset runs for the broader precision qualification.

The observed cost is consistent with the decomposition, rather than with
polynomial degree alone. `inspect_tree.py` mirrors the frozen tree construction
and counts the shared basis and output-product branches:

| Profile | Shared-basis ciphertext products | Total ciphertext products per real channel |
|---|---:|---:|
| Degree 95, baby 16 | 14 | 44 |
| Degree 63, baby 8 | 8 | 62 |
| Degree 63, baby 16 | 13 | 49 |
| Degree 63, baby 32 | 24 | 54 |

These are static operation counts, not GPU kernel measurements. The six-level
degree-63 tree is more constrained by the available leaf depth. Reducing its
serial depth therefore still increases total ciphertext products relative to
the seven-level degree-95 tree. Instrumented DigitExtract across both channels
takes median 352.980 ms instead of 274.904 ms; this is the main observed latency
increase. Grouped stage medians include instrumentation and need not sum to
the unobserved total. No latency improvement is claimed.

## Validation and reproduction

All 44 cells, including both `+/-2^-12` boundaries, are tested against an
independent 100-digit Clenshaw fixture under the dense application secret.
The initial run observes extraction evaluation error `2.4439e-6` relative to
that oracle, and final integer residual `1.07322e-12`. Reuse, aliasing and
stable live allocations pass. End-to-end qualification covers all 32768
complex slots and uses a maximum message error below `2^-18`.
With the final baby step 16, the all-cell run observes extraction evaluation
error `3.14281e-6` and integer residual `1.20706e-12`.
All 15 retained CTest targets pass in 972.05 seconds, including the native
MEBOOT and ordinary Discrete paths. The inherited `basic_test` exclusion is
retained for its documented preexisting timeout. Five certificate/metadata
tests, six arithmetic-reference tests and Go vet also pass.

Evidence and performance results are recorded in
`../../evidence/discrete-identity/remez-20261016/`.

```sh
# Run from the vendored Lattigo v6 module snapshot described above.
go run /absolute/path/to/cheddar/tools/research/generate_discrete_extractor_remez.go \
  --degree 63 --max-abs-integer 21 --bits 6 --max-iterations 120 \
  --output /absolute/path/to/cheddar/parameters/research/discrete_direct6_k21_remez_d63.json

# Run from the Cheddar worktree; regenerate the certificate after any byte change.
python3 tools/research/certify_discrete_extractor.py \
  --manifest parameters/research/discrete_direct6_k21_remez_d63.json \
  --output docs/evidence/discrete-identity/remez-20261016/direct6-certificate.json
python3 tools/research/test_certify_discrete_extractor.py
python3 tools/research/profile_discrete_depth.py \
  --out NEW-CAMPAIGN --name d63 --keysets 3 --repetitions 30 --warmups 5 --profiles 5
python3 tools/research/profile_discrete_depth.py \
  --out NEW-CAMPAIGN --name d95 --legacy-d95 \
  --keysets 3 --repetitions 30 --warmups 5 --profiles 5
```
