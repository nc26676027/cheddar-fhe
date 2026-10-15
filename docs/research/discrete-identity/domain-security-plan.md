# HW40/K20 integer domain and security plan for Discrete Identity

## Current six-bit candidate audit

`tools/research/audit_meboot_discrete.py` binds the candidate to the exact
Identity19 Q/P, temporary q*, entry modulus and the recorded secret/error
domains. Its output is
`docs/evidence/discrete-identity/meboot-discrete-parameter-audit.json`.
The current candidate changes scales, polynomial depth, output level and
rotation requirements, but keeps the exact public modulus domains. The
main screen already assumes unlimited samples. Reuse remains a heuristic
screen under that inherited model, not a new estimator run or security proof.

The six-bit degree-95 extractor is certified on every full radius-2^-12
cell centered at [-32,31], not just K20. For a common centered Q0 lift and
fixed-weight ternary H40, each component coefficient is bounded by q0/2;
the lifted phase is bounded by 41*q0/2. Subtracting a centered residual
phase, also bounded by q0/2, gives the integer bound |I|<=21. Thus the
six-bit signed representation itself covers this deterministic bound.
This does not prove the much narrower fractional radius after approximate
CtS, or bound complex/encrypted arithmetic errors.

The K20 Irwin-Hall calculation below is retained as a separately labeled
conditional reference. It is not needed to exclude integers between 20
and 21 from a polynomial that actually covers [-32,31]. Its independence
model and omitted entry noise must still be disclosed if it is cited.

The prior coefficient-unit gap 512 below is different from the final
application-message gap. Including StC's physical gain 1/64 gives
`g=q0/((1/64)*2^33)`, about 32768, for final identity normalization.

Status: independent analytic audit, 2026-10-15. This is a conditional
reliability calculation and a security rerun plan, not a Discrete cleaner
qualification or a new lattice-estimator result. No encrypted run is implied.

## Source binding

| Source | What it establishes |
| --- | --- |
| Choe, Kim, Stehle and Suvanto, *Leveraging Discrete CKKS to Bootstrap in High Precision*, CCS 2025, local `build/research/discrete-ckks-2025-1786.pdf`, SHA-256 `30054c46e31b81fc8d21433ca633ecd934d8d878a37c8d3f6b228745dd00b4d6` | Sections 2.1, 3.1-3.2 and 5.2 (PDF pp. 5-8, 11-12) give the raised integer, DigitExtract input, gap, signed digit convention and security/failure distinction. Its implemented five-bit setting is `H=30`, 32 periods, signed `[-16,15]`; its `H=30` deterministic phase bound does not apply to `H=40`. Its direct five-bit degree-47 polynomial and depth six are not a six-bit design. |
| MEBOOT `../paper1-linear-bootstrap`, HEAD `6330f64b178b6215edd35baf5b11c8da558ca95c`; `paper/sections/joint-selection.tex`, SHA-256 `41cd3b9568667bf5d32a5a118a0e89cfa0f072eac1a550c9064d0c1a80c093a1` | EC21-based Irwin-Hall model, its independence assumption and the separate correlation-safe union bound. The manuscript's selected `H40/N'=8192` result is **K27**, not Cheddar's later K20 point. |
| This worktree `codex/discrete-identity`, base HEAD `94f33e9791e0e312c28fa80fe36f5f9fd0d424f4`; `parameters/identity19_meboot.json`, SHA-256 `1daeb8f5e5eda12dbdf39dda600fb0b92232afedc64a30a6b7d1de8b946176a2` | Frozen current `N=65536`, extension eight, `N'=8192`, subring `H=40`, `K=20`, entry `q0=4398044938241`, `P_ent=786433`, base-16/11-digit entry, and normal Q/P arrays. `unittest/tools/audit_identity19.py` supplies the existing conditional Chernoff computation; `docs/evidence/identity19/security.json` (SHA-256 `faa6a5fd58ed3cc7c893b2c8c587ef97e5f8cb612a596d270f894f381f0cb727`) binds the old-chain attack records. |
| Adjacent frozen security package `paper/evidence/measurements/submission-security-20261007/security-resolved.json`, SHA-256 `4b63bdfcac9923feffb17ae96afd54fb219fdc94d167b2d4af1596e97d262a11`; its `identity16-18-joint-plan/identity-results.json`, SHA-256 `3f3a7bf82a64e897063415d47150da0c0205b317ba4a81aaf400b42337c37fd2` | Exact original public-key domains, attacks and sample allowance, not a certificate for a changed Discrete modulus chain. |

## Coefficient event and six-bit contract

Let `X = U_1 + ... + U_41`, where **each** `U_i` is independent
`Uniform(-1/2,1/2)` under the EC21 coefficient model. The count is `H+1=41`:
40 nonzero subring-secret terms plus the constant term. It is neither 8192
nor 65536. The support is `[-20.5,20.5]`. This is a model of a normalized
raised phase coefficient, not a theorem that every actual encryption and
entry-switch phase has that law. In particular, fixed-weight secret positions,
shared convolution coefficients, encryption randomness and the selected
entry switch must be checked against the model's independence and uniformity
premises. Subring extension repeats coefficients; it does not produce eight
fresh independent coefficient draws.

Six signed binary digits with the conventional two's-complement convention
encode integers `[-32,31]`, so `[-20,20]` is representable. For a centered
period-64 extraction, `-32` and `+32` identify at the periodic boundary;
the extractor must use an explicit signed representative and prove its valid
interval, including the negative endpoint. A numerical approximation radius
`K=20` is narrower than the representable range. It is **not** a claim that
the direct five-bit polynomial in the Discrete paper can be widened, that
six digits cost six multiplicative levels, or that the interval endpoints
are numerically safe. Constructed six-bit digit polynomials, cleaning,
recombination and the evaluation graph need their own error/depth audit.

Use `t=K-1/2-2^-10=19.4990234375` as the **existing conservative phase
threshold**. It reserves 0.5009765625 units before the radius; for an
integer `I`, `|I|<t` entails `|I|<=19` and leaves at least one unit to
`K=20`. The half-cell is a deliberate safety margin, not a measured noise
bound. To transfer the event from ideal `X` to a cleaner input
`z=I+epsilon+e`, first establish a deterministic or probabilistic budget
for fractional message `epsilon`, entry-switch error, rounding/encoding,
C2S approximation and scale/gain mismatch. If their combined magnitude can
exceed the available margin, recompute the tail at a smaller `t` and add
the probability of the error-budget failure. Correlated fork errors must be
tracked together; treating their subtraction as independent noise would
discard the shared pre-fork phase. See `meboot-integration-contract.md`,
"Integer representative and unit binding," and the MEBOOT manuscript's
`paper/sections/interfaces.tex` error proposition.

The Discrete paper writes the raised input as `I+(Delta/q0)m` before digit
extraction and calls `g=q0/Delta` the gap (Sections 3.1, 5.2). The current
Cheddar entry output scale is `Delta=2^33`, so `g=511.999816894648` and
`Delta/q0=0.00195312569849`. If a coefficient message were bounded by one,
its fractional term alone could be slightly **larger** than `2^-10`; the
existing `2^-10` guard is not automatically justified by the scale. A
coefficient bound of `0.25` would give about `0.000488281425`, before other
errors, but the old signed-grid *slot* interval `[-0.25,0.25]` is not a
uniform bound on post-StC *coefficients*. Derive the actual coefficient
bound and entry/C2S error in common physical units before assigning the
guard. The high-scale bypass and low branch share `I` only if the exact
fork/normalization contract is preserved.

## Independent exact tail and counting

For `n=41`, put `a=n/2-t`. Symmetry and the Irwin-Hall CDF give
`p_single(t)=Pr(|X|>t)=2/(41!) * sum_{j=0}^{floor(a)} (-1)^j C(41,j)(a-j)^41`
for `0<=t<=20.5`. At the chosen threshold `a=1.0009765625`, this simplifies
exactly to `2*[a^41 - 41*(a-1)^41]/41!`. At `t=20`, it is
`2*(1/2)^41/41!`. Decimal logs below are evaluations of these formulas;
the tiny subtraction at `a=1+2^-10` does not affect displayed digits.

| Threshold `t` | `log2 p_single`, exact | `log2 p_single`, Chernoff upper bound | `log2` bound after `2^18` union |
| --- | ---: | ---: | ---: |
| `20` | `-204.516592` | `-201.509136` | exact `-186.516592`; Chernoff `-183.509136` |
| `19.4990234375` | `-163.458856` | `-160.451400` | exact `-145.458856`; Chernoff `-142.451400` |

The Chernoff column independently follows
`inf_{lambda>0} 2 exp(-lambda*t) [sinh(lambda/2)/(lambda/2)]^41`;
the stationary equation is
`coth(lambda/2)/2 - 1/lambda = t/41`.
`unittest/tools/audit_identity19.py` and `docs/evidence/identity19/security.json`
record its two whole-refresh bounds. The exact Irwin-Hall result tightens
the numeric upper bound by about 3.007 bits **under the same model**; it
does not repair an unjustified model or guard.

| Count convention | `M` per refresh | `log2` exact union at `t=19.4990234375` |
| --- | ---: | ---: |
| Distinct subring coefficients | `N'=8192=2^13` | `-150.458856` |
| Literal EC21 ring convention | `2N'=16384=2^14` | `-149.458856` |
| Conservative full-ring lift | `2N=131072=2^17` | `-146.458856` |
| Frozen Cheddar audit factor | `2^18=262144` | `-145.458856` |

Cheddar's `2^18` is a conservative factor twice the manuscript's full-ring
`2N`, not a count of independent subring draws. For `R` refresh calls, the
correlation-safe bound is `min(1,R*M*p_single)`; independence among
coefficients, real/imaginary channels or refreshes is unnecessary **once a
valid common marginal bound exists**. With `M=2^18`, the Chernoff result
meets `2^-128` for `R<=22402` and the exact-model result for `R<=180152`
(rounded down from the displayed-log calculation). These are model-conditional
ceilings, not an application-wide allowance: allocate a smaller share if
other failure events consume the same budget. At `R=1` the exact bound is
`2^-145.46`; the old Chernoff bound is `2^-142.45`. Repeated calls should
use their actual count and any changed coefficient count, without multiplying
failure probabilities as if the calls were independent.

## Public-key exposure and changed-chain screen

Current frozen screens are two different public domains. Entry uses
`N'=8192`, fixed-weight independent-sign `H=40`,
`q0*P_ent=3458767674915684353` (`log2=61.58496382`), sigma-3.2 key
error and `J*N=11*65536=720896` structured coefficient equations from
eleven base-16 full-ring entry-key rows. Main uses `N=65536`, dense
fixed-weight independent-sign `H=2048`, full **normal** `Q*P`
(`log2=1715.00995021`) and unlimited samples. The existing classical
BDGL16 minima are entry `136.379840655` (BDD meet-in-the-middle hybrid)
and main `132.466948663` (dual hybrid), at lattice-estimator revision
`53da5982597709ba0fdf94ea37a84d822310fd84`; see the source package
README and `docs/evidence/identity19/security.json`. They are heuristic
attack-cost screens under the recorded sparse-secret and Gaussian model,
not domain-escape probabilities or unconditional security. Structured ring
equations are not thereby asserted statistically independent. A runtime
full-Q subring ciphertext is not automatically a third public key domain;
new keys or samples are.

For a proposed Discrete chain, first export a **new, immutable public-only
plan** containing every exact Q and P integer, q*, q0, `P_ent`, digit width
and row count, secret weights/distributions, error sampler, and all public
evaluation/relinearization/rotation/entry key exposures. Recompute the
largest exposed `Q*P` and `q0*P_ent`, effective dimensions and sample
allowances; account for any added keys or changed entry decomposition.
Record the plan hash, constructor/source revision, command and environment.
Do not relabel the frozen Identity16-18 record or assume that retaining K20
keeps the same public modulus. The existing Cheddar
`unittest/tools/audit_identity19.py --security-package ...` checks **equality
to the old Q/P**, so it cannot qualify a changed chain by renaming a file.

After the new plan is produced, the existing adjacent analysis runner is
`paper/tools/analysis/security/run_lattice_estimator.py` (SHA-256
`b2387a13bb93db0e2b4e119cc1c5034cb14449ce9686d7acc86640a3d9c38c3e`).
Its documented invocation, from `../paper1-linear-bootstrap`, is:

```text
python paper/tools/analysis/security/run_lattice_estimator.py \
  --plan <new-public-plan>/identity-results.json \
  --outdir <fresh-research-worktree-evidence-directory> \
  --estimator <pinned-sage-lattice-estimator> --all
```

The runner expects the public `identity-compare -plan` schema; an equivalent
new plan producer must bind Discrete's changed key domains before this
command is meaningful. Keep the new plan and results **in this research
worktree**, leaving the adjacent package untouched. Use the recorded Sage
10.9 environment, six named classical attacks per domain, exact
`SparseBinomial(eta=1,H,n)` secret law, sigma-3.2 error only if the
implementation still uses it, and the pinned estimator revision for a
like-for-like comparison. Verify all attacks completed, including the
MATZOV/wide-real path, inspect raw minima without rounding up to 128,
and separately record estimator assumptions and unsupported attacks.
If a new ring/secret or error law is unsupported, label it unresolved
rather than substituting the old model. A successful screen still leaves
the digit-circuit error, domain guard and encrypted precision gates open.

## Decisions before implementation claims

1. Specify the exact signed six-bit map and constructed polynomial circuit,
   including domain endpoints, degree, multiplicative depth and key needs.
2. Bind the actual raised integer and coefficient gap after StC, entry switch,
   four C2S roles and both fork branches; bound `epsilon+e` and recompute `t`
   if the `2^-10` guard fails.
3. Prove or empirically characterize the marginal lifted-phase law under the
   actual key/entry process; use a valid tail for that law, then apply the
   correlation-safe union to the intended number of refreshes.
4. Produce a changed-chain public plan and fresh exact-domain estimator
   records before making a 128-bit lattice-security claim.
