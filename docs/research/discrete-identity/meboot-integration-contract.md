# MEBOOT interface contract for Discrete CKKS Identity

Status: source-derived integration contract, 2026-10-15. No Discrete CKKS
encrypted implementation, modified-chain security estimate, or GPU result is
claimed here. The task attachment requests two selectable identity paths; this
note fixes only the MEBOOT side of their shared integer-cleaning interface.

## Source identity and authority

| Source | Identity and scope |
| --- | --- |
| Adjacent MEBOOT repository | `../paper1-linear-bootstrap`, commit `6330f64b178b6215edd35baf5b11c8da558ca95c`, read-only. `paper/main.tex` (SHA-256 `3b11396fced02942aa285335f164b54eb9b67348a13ac934dae791b333382d6d`) is the current manuscript source. Its `sections/interfaces.tex` and `sections/gadget.tex` hash to `b18d68fd600d1c5d69b9f0fa15df8873612e9856a2b5f8343e70c322c85957f3` and `1d37f756bf62bb31caff19536fb469cffe0762131acca2636baa2ef643725c21`. The available `paper/build/main.pdf` hashes to `270752a7d77d504097996771b3f2557f5b759fc4e59e50d30afb8ea2ea1f4c2d` and predates the present source file timestamp; use the TeX for current claims. |
| MEBOOT claim binding | `paper/evidence/claim-evidence-inventory.md` and `paper/REPRODUCIBILITY.md` identify manuscript displays, the unique result record, and the frozen submission-security package. `paper/evidence/measurements/submission-security-20261007/security-resolved.json` hashes to `4b63bdfcac9923feffb17ae96afd54fb219fdc94d167b2d4af1596e97d262a11`. These records bind **evaluated manuscript configurations**, not every future chain. |
| This Cheddar worktree | `codex/discrete-identity` at `94f33e9791e0e312c28fa80fe36f5f9fd0d424f4` before this note. `parameters/identity19_meboot.json` hashes to `1daeb8f5e5eda12dbdf39dda600fb0b92232afedc64a30a6b7d1de8b946176a2`. Its pinned, original Go export in `parameters/reference/selected-original.json` hashes to `470df5f9ba3d18a8b0e3f003e1d4bda4e7557a6021ad3767d68e12a0894def58`; the separate K20 Mod1 reference hashes to `0486ff0d24515dda43f44c61664fe002112e5949ca7a17f365f6900061ddb387`. Current `docs/evidence/identity19/security.json` hashes to `faa6a5fd58ed3cc7c893b2c8c587ef97e5f8cb612a596d270f894f381f0cb727`. |
| Assignment | The full local attachment `C:/Users/26676/.codex/attachments/a9a16582-1f1a-420a-8094-8a116f437e50/已粘贴的文本.txt` hashes to `4cd7a3d72ae46fdea2558e709a6b069ec8c96517d438fe27dc521ea307ff3326`. It supplies the proposed Discrete integration goals, not experimental evidence or a mathematical proof. |

The manuscript's measured Identity16-18/Q32/P5 joint point uses HW40/N'=8192
but **K27/DA3 and output L20**, as its `sections/joint-selection.tex`,
`sections/evaluation.tex` and `meboot/identity/presets/identity16_selected.go`
show. Cheddar's Identity19 K20/degree62/DA2/output L21 is a later native
retuning on the selected chain. The historical `meboot/identity/presets/identity19.go`
is a different experimental constructor (HW64/extension16 in its joint branch);
its name is not proof that it implements this Cheddar Identity19 point.
Do not transfer either manuscript timing or historical constructor security
classification to the Discrete variant.

## Frozen entry and four-role C2S

For the current Cheddar identity point, `N=65536`, `n=N/2=32768` complex
slots, `N'=8192`, subring-secret Hamming weight `H=40`, dense application
secret weight `2048`, `q0=4398044938241` (42 bits), distinct
`P_ent=786433`, and `q*=9007199255658497`. The entry public modulus is
`q0*P_ent=3458767674915684353`, or about 61.585 bits. These exact integers
come from `parameters/identity19_meboot.json`, `src/extension/BootParameter.cpp`
and `docs/evidence/identity19/security.json`; the main hybrid-switch `P` is
the separate five-prime profile array. Entry decomposition is `w=4`, `B=16`,
`J=ceil(log2(q0)/4)=11` unsigned digits, including the bounded top digit.
The source is the dense secret and the target is the extension-8 subring
secret. Entry rows satisfy
`b_j+a_j*s_sub = B^j*P_ent*s_dense+e_j (mod q0*P_ent)`; componentwise
modulus-down returns a ciphertext at `q0` under `s_sub`. Key error is sampled
over the full ring. The paper gives conditional coefficient and RMS error
bounds, with the centered rounding term bounded by `(1+H)/2`; it does not
make `P_ent` interchangeable with main `P`. See manuscript `sections/gadget.tex`
Eq. `entry-digits`, `entry-identity`, `entry-bounded-noise`,
`entry-rms-noise` and `sections/appendix-gadget-proofs.tex`.

The source algorithm in `sections/interfaces.tex` (`alg:meboot-schedule`,
Propositions `prop:level`, `prop:prefix`, `prop:lcr`) fixes the following
states. `L=31` is the top normal main-Q level for this Cheddar profile:

| State | Secret, modulus/level and role |
| --- | --- |
| S2C, ScaleDown, EntrySwitch | Dense application input L2/scale 2^40 becomes coefficient ciphertext L0; ScaleDown integer multiplier is 1. EntrySwitch moves to `s_sub` at `q0`. |
| `c_up` and fork | Centered lift/OverRaise produces `c_up` under `s_sub` at `Q_L*q*` (Cheddar L32). **Before M1**, save `c_up|Q_L`: componentwise RNS residue restriction to the main-Q primes, retaining scale and packing. Switch that saved state to `s_dense` for the independent high-precision bypass. This is not an extra rescale. |
| M1 | Width 3, extension 8, key-free automorphisms preserving `s_sub`; one rescale removes **q***, yielding main-Q L31 under `s_sub`. The maximal key-free width is `log2(n)-log2(N'/2)=15-12=3`; the remaining widths total 12. |
| M2 | Width 4 LCR+AKS reconstructs the top main-Q residue and switches `s_sub -> s_dense`, conserving L31. It requires its encoded-diagonal phase bound and the same rounded integer representative in the lower residues and reconstructed top limb. |
| M3 / M4 | Widths 4 and 4, dense BSGS under `s_dense`, one main-chain rescale each: L31->L30->L29. Cheddar's current dense bases are 64 and 4. |
| Recovery | Low branch is C2S at L29; bypass C2S starts from the separately switched main-Q fork and remains under `s_dense`. Each full-slot branch splits real/imaginary channels. A cleaner may replace the nonlinear integer-removal map only after its input and output units have been bound. |

The paper's level result is the *role invariant*: M1 spends q*, M2 spends no
main-Q prime, and M3/M4 spend two. It does not imply the Discrete cleaner has
the present K20 polynomial's six levels plus DA2. Cheddar's current numerical
path continues L29->L23->L22->L21 and gives L21/scale 2^40; Ordinary uses
K16/degree30/DA3 and ends L20. See
`docs/research/identity19-verification-20261015.md` and
`src/extension/MEBootExecution.cpp`/`BootTransforms.cpp`.

## Integer representative and unit binding

The source phase after the raise is `m+e+q0*I`, with integer polynomial `I`
(`sections/background.tex`). It is the **same** lifted phase that feeds the
low C2S branch and the pre-M1 main-Q bypass fork. Restricting `c_up` to
`Q_L` preserves residues; independently re-raising, re-encrypting, or
rescaling a fork can change its representative and invalidate subtraction.
The manuscript's direct-transition bound defines the implemented centered
residue interval `I0=[-h0-1,h0-1]` for `h0=(q0-1)/2`, and requires both
encoded-diagonal small-phase inequalities and
`a21*B0+(a2+1)/2 <= h0-1`. This ensures the rounded lower residues and LCR's
reconstructed top residue name the same integer. Residual-ring uniformity
additionally requires a unit subring secret and digit ideal generating
`R_q0`; the proof explicitly **does not certify** the selected QP gadget
after modulus-down. See `sections/interfaces.tex` Eqs. `phase-condition`,
`representative-condition`, `lcr-entry-uniformity` and
`sections/appendix-interface-proofs.tex`.

For Discrete Identity, a candidate common-unit model is
`z_low=I+epsilon+e_low`, `z_high=I+epsilon+e_high`,
`I_clean=CleanInteger(z_low)`, and `y=z_high-I_clean`, followed by the
paper-specific normalization. This is an **integration invariant**, not an
observed output. Bind the actual physical CtS gain, CKKS scale, secret,
packing, level, and normalization of *both* branches and the cleaner before
subtraction. In the current native profile, StC intermediate/output scales
are 2^37.875/2^33, lift multiplier is 32768, effective lift scale is 2^48,
proper CtS gain is twice the pinned original, and Mod1 return gain is 1/2.
The current `0.25` bypass gain is independent. These are recorded in
`parameters/identity19_meboot.json` `native_scaling_binding`, and checked by
`BootParameter::ValidateScalingBinding()`. They are **not** a proven Discrete
cleaner scale schedule. The manuscript's general recovery is
`y=G(z_low)-z_low+z_high` in common units; its `prop:error` explicitly adds
deterministic error for a scale mismatch and notes that pre-fork entry noise
is shared by the branches.

## Reliability and security scope

The present K20 range audit models a coefficient as the sum of `H+1=41`
independent centered `Uniform(-1/2,1/2)` variables. With threshold
`K-1/2-2^-10=19.4990234375`, its two-sided Chernoff bound is
`log2 p <= -142.4513995` after the recorded whole-refresh union factor
`2^18`; at K20 the recorded value is `-183.5091355`. See the Cheddar
Identity19 report and `docs/evidence/identity19/security.json`. This is a
conditional domain-escape model, **not** a measured frequency or RLWE
security bound. The manuscript `sections/joint-selection.tex` gives the
Irwin-Hall single-coefficient model and distinguishes its independent-
coefficient call formula from the correlation-safe union bound. Subring
extension repeats coefficients; do not count repeats as independent samples.
The union bound needs a valid marginal tail but does not need independent
coefficients or refreshes. Entry key-switch noise, rounding bias, and
correlation between the shared fork branches are not included in the ideal
Irwin-Hall summands. A six-bit signed representation for K20 is only a
candidate; its signed endpoint, extraction polynomial, actual depth and
cleaning error must be established independently.

The exact **current** entry and main public-key domains were rebound to
the 2026-10-07 source records in `docs/evidence/identity19/security.json`:
entry `N'=8192,H=40,log2(q0P_ent)=61.58496382`, 720896 structured entry
coefficient equations, minimum recorded classical estimate 136.3798407;
main `N=65536,H=2048,log2(QP)=1715.00995021`, unlimited-sample minimum
132.4669487. The lattice-estimator revision is
`53da5982597709ba0fdf94ea37a84d822310fd84`, with classical BDGL16
costs and the disclosed sparse-secret/error model. Source configuration
names remain `identity16-18-joint`. The source README explicitly says ring
equations are not asserted independent and infinite search output is not an
attack-cost proof. **Any changed Discrete Q chain, special P, entry modulus,
secret distribution, error, or sample exposure needs a new exact-domain
screen.** Existing Identity19 estimates and encrypted precision do not
qualify it; numerical range and lattice cost are separate gates.

## Discrete cleaner source search and integration limit

The adjacent MEBOOT `meboot/` tree implements its own Go/Lattigo bootstrap,
CosDiscrete EvalMod and a separate RBOOT function; it does not provide a
source-bound Discrete CKKS `DigitExtract`/`VecClean` integer cleaner. Its
manuscript cites Choe et al. and its literature audit records ePrint
2025/1786, `Direct2Cln19` and source-specific metrics in
`paper/evidence/references/identity-and-cnn-novelty-20261007/README.md`.
That citation is **not** a derivation of the Discrete circuit or permission to
copy the MEBOOT CPU evaluator. A separate nearby `advanced_bootstrap` roadmap
reports a partial Hermite small-K cleaner and explicitly defers full
homomorphic digit extraction (`DigitExtraction=false`); it is neither the
Discrete paper's authoritative implementation nor an independent paper
substitute. Derive the baseline extractor, VecClean and recombination from
the Discrete paper before asserting any MEBOOT-Discrete compatibility.

The immediate acceptance boundary is therefore a recorded state tuple for
each side of the fork: `(secret, exact Q/P/q* basis, level, physical gain,
CKKS scale, packing, integer convention, error bound)`. Only after the
independent Discrete Identity baseline passes may its cleaner replace `G`
and the exact modified-chain security and correlation assumptions be
re-evaluated. No CPU runtime is copied into Cheddar by this contract.
