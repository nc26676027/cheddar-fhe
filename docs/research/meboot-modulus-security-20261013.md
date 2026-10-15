# MEBOOT / OverModRaise modulus budget and security screen (Cheddar, 2026-10-13)

2026-10-15 scope correction: the HW256/N'=4096/K64 calculations below concern
the retained classic profiles, not selected Identity16 joint4434. Its HW40,
extension-8, N'=8192, low-q0P entry and K20/degree62/DA2 verification are recorded
in `identity19-verification-20261015.md`. Do not transfer the old sparse
weight, entry-noise multiplier or K requirement to that selected route.

Scope: primary-source and code screen of MEBOOT / OverModRaise, the author
reference implementation (`D:/code space/paper1-linear-bootstrap`), and Cheddar
(`D:/code space/cheddar-fhe`). Structural modulus-budget compliance is kept
separate from heuristic lattice estimates; neither is a 128-bit security
certificate. No code changed for this report, and no GPU or estimator run was
made by the report author. Raw evidence is in the git-ignored snapshot
`.scratch/agent-planning/meboot-modulus-security-20261013/` (see
`sha256sums.txt`).

STATUS: structural alignment and precision/latency closed; security is a
heuristic screen. Current profiles: 32-bit log2 Q_key P = 1743.3121404102606
(reallocated chain, 52 main / 6 terminal / 12 auxiliary), 64-bit =
1657.0001437774185; standard / MEBOOT outputs are 2 / 4 (32-bit) and 4 / 6
(64-bit). Six-attack entry screens (n=4096, h=256, DtS-only sigma 819.2): min
129.750228 (32) / 130.954127 (64). Dense normal-key-basis screens: five attacks
done at both widths, min 130.411543 (32) / 137.883005 (64); the `dual_hybrid`
row is `oo` at both widths (not a bound) and dense `bdd_mitm_hybrid` is
permanently recorded INCOMPLETE -- the two `--dense-mitm` sidecars ran >50
minutes at 100% CPU inside a Sage/GiNaC `Expression.sqrt` on the ~2^1743 /
~2^1657 public modulus and were terminated on 2026-10-13T17:12+08 with no
result -- so no dense-MITM number exists and none may be quoted. Precision:
direct EvalMod passes both widths, and the final 30-sample paired run on the
frozen tree passes every gate -- 32-bit standard 94.32 ms / 134.030658 dB vs
MEBOOT 92.94 ms / 134.033378 dB, 64-bit standard 98.69 ms / 124.900613 dB vs
MEBOOT 96.30 ms / 125.030294 dB -- with 11/11 regression targets at exit 0.
The bounded-CRT interface has a conservative floating-point diagnostic (not a
certified interval proof) that passes at both widths. This is not a 128-bit
security certificate.

## 1. Primary sources, reference implementation, estimator tooling

- MEBOOT paper: `paper/main.tex`, commit
  `a4415572d9557c38b92cebe0da56a35bea705c54` of
  https://github.com/nc26676027/paper1-linear-bootstrap ("MEBOOT:
  Modulus-Efficient CKKS Bootstrapping via Role-Aware C2S"). No public
  ePrint/DOI exists as of 2026-10-13; the commit-pinned source is the paper of
  record. Reference implementation: `meboot/` (Go) plus the frozen Lattigo v6
  baseline `df6db08359185da74b7d5626e834d274ac2745d1`, with paper-owned
  successor patches in `third_party/lattigo/PAPER1-PATCHES.md`. Cheddar:
  https://github.com/nc26676027/cheddar-fhe.
- Mechanisms: OMR https://eprint.iacr.org/2025/1298 ; LCR / single-digit AKS
  https://eprint.iacr.org/2025/1403 (PKC 2026); subring secret encapsulation
  https://eprint.iacr.org/2025/1594.
- Estimator for every Cheddar row: pinned checkout
  `53da5982597709ba0fdf94ea37a84d822310fd84`, driven through the paper adapter
  `run_lattice_estimator.py` (sha256 `3a5fb411...`), `BDGL16`/GSA,
  `Xs = SparseBinomial(eta=1, hw=H, n=N)`, `Xe = DiscreteGaussian(sigma)`,
  `m = oo`; the compatibility context is exact for Lattigo
  `ring.Ternary{H}`. Repo-side driver: `unittest/tools/screen_meboot_security.py`
  (sha256 `286f5b56...`), which records the profile/adapter/estimator hashes in
  every output JSON. Screens from different estimator revisions must not be
  merged. A `dual_hybrid` `oo` row means the dual/FFT path found no admissible
  finite cost (a former main-domain `oo` was a Python-float overflow of
  `Xs.stddev * q`, since promoted to 256-bit reals); either way `oo` is not a
  security level and not a bound.
- Sibling calibration (separate pinned run, rev
  `8d38f52c0bcc46f23d697c9c592bad50df0b124b`; `screen_e2_security.py`
  `6435ac57...`, summary `b987ffea...`): ordinary-entry n=65536 h=32 Q0*P0 min
  173.408; joint-entry-ext16 n=4096 h=64 Q0 min 136.913; main n=65536 h=1024
  Q/P min 130.969. It labels itself
  `PUBLIC_ATTACK_SCREEN_NOT_COMPLETE_SECURITY_QUALIFICATION` (three `dual`
  `NotImplementedError` rows, three non-finite rows, `any_below_128: false`).

## 2. Structural budget (recomputed from the current JSONs)

Values recomputed from `parameters/bootparam_40.json` (sha256 `076cb828...`)
and `parameters/bootparam_40_64bit.json` (sha256 `ffe38f41...`) using the C++
semantics (`src/core/Parameter.cu:97-116,298-311,345-352`;
`src/UserInterface.cu:146-148,166-170,365-366,648-657`). Reproduction script
and log: `verify/verify-current-profile.py` / `.log` in the snapshot.

| item | 32-bit | 64-bit |
| --- | --- | --- |
| main / terminal / aux primes | 52 / 6 / 12 | 23 / 1 / 7 |
| max level / key ceiling / prefix level | 21 / 20 / 21 | 23 / 22 / 23 |
| standard CtS end / ME end | 16 / 18 | 18 / 20 |
| log2 Q_key | 1371.3851893657818 | 1230.000143777459 |
| log2 P | 371.9269510444788 | 426.9999999999596 |
| log2 Q_key P | 1743.3121404102606 | 1657.0001437774185 |
| prefix primes | 1085276161, 1086455809 | 36028797054615553 |
| prefix log2 | 60.032397402578 | 55.000000001428 |
| log2 Qover P | 1803.344537812838 | 1712.000143778846 |
| per-level drop below the prefix | 33.9-54.1 (levels 1-5), 70.7-72.9 (6-20), 60.03 (prefix) | 40.0 (1-7), 60.0 (8-22), 55.0 (prefix) |
| DtS entry basis log2 Q0P | 112.013493 | 111.000000 |
| sparse h / dense h | 256 / 32768 | 256 / 32768 |
| double-angle count / default enc level | 5 / 5 | 5 / 7 |
| outputs standard / ME | 2 / 4 | 4 / 6 |

Composition, from `reallocate_phi_budget.py` (sha256 `362c727c...`) and the
JSONs: the 32-bit profile keeps the original baseline level-0..2 layout
(`main_primes[0] = 974258177` plus the level-0 terminal pair) and rebuilds the
rest as 6 new ~27-bit StC primes used as three ~54-bit rescale pairs at levels
3-5, then 45 new small NTT primes used as fifteen 71-73-bit triples at levels
6-20, and a prefix level 21 = [52, 6]. Its Q_key P (1743.3121404102606) is
2.1036 bits below the original 1745.4157367 baseline. The 64-bit profile keeps
the old `main_primes[0..7]` layout, adds 15 new 60-bit main primes and 7 new
61-bit aux primes, and its Q_key P (1657.0001437774185) is 20.0 bits above the
original 1637.0003304 baseline; that raise is part of the high-precision chain
and is recorded in the chain logs. Superseded history, not current: the
`generate(31,30)` variant at 1670.7534702681287
(`high-precision/layout-high-precision.json` `a863f2af...`, log
`c84d28a8...`) and the `generate(30,30)` pre-reallocation profile at
1640.2723467441613 (`profile-high30-before-reallocation.json` `42f2acda...`).
There is no second current 32-bit configuration.

Structural compliance is a code property, independent of any estimate:
`GetKeySwitchNP(level)` rejects levels above `max_key_switch_level` ("Key
switching is forbidden at OverModRaise levels", `Parameter.cu:298-300`); the
constructor requires every level above the ceiling to keep the key-basis main
count and only add terminal primes (`Parameter.cu:97-102`); `q_primes_` stores
terminal primes reversed ahead of main primes (`Parameter.cu:115-116`);
ordinary encryption asserts the plaintext NP is a subset of
`GetKeySwitchNP(max_key_switch_level_)` (`UserInterface.cu:166-170`); and
ModSwitch decomposition chunks take the normal key-chain terminal origin at or
below the ceiling and the full terminal count above it
(`ModSwitch.cu:180-190`). For the 32-bit JSON, the two prefix primes are
`terminal[4:6]` and exist only at level 21; for 64-bit the single prefix prime
is `terminal[0]` and the key chain has num_ter = 0 at every level at or below
the ceiling. `unittest/tools/audit_meboot_modulus.py` asserts the same
schedule. All of this is structural modulus-budget compliance only and says
nothing about 128-bit security; the estimator screens in Sections 4-5 are a
separate, heuristic gate.

## 3. Why standard CtS key-switches only within Q_key P; key-free prefix; LCR saving

Standard CtS begins with key switches whose evaluation keys are RLWE samples
over Q_key P, so the entering ciphertext cannot exceed the key ceiling (20 / 22
here); it consumes four main levels and ends at 16 / 18. MEBOOT raises after
EntrySwitch and runs Stage 1 / M1 (diagonal evaluation plus q* rescale) without
reading a key. Prop. 2 (`interfaces.tex:72-82`) gives the maximal key-free
prefix `m1* = nu - nu_sub`; wider prefixes would request automorphisms outside
the subring and need auxiliary keys. The ModUp lift is defined modulo
Qover P = (Q_key + prefix) * P (1803.34 / 1712.00); Stage 2 descends to
`sk_den` and uses keys at or below the ceiling. Prop. 1
(`interfaces.tex:61-70`): low-precision C2S consumes q* once plus exactly two
main-chain primes instead of four normal levels, so at equal entry the MEBOOT
chain keeps two more levels (18 vs 16; 20 vs 18). The raised intermediate is a
deterministic ModUp lift, not a fresh RLWE encryption, so fresh-Gaussian-at-
Qover estimates are withdrawn as inadmissible; Qover is a storage/modulus
fact, not an LWE sample domain.

## 4. Entry (Dense-to-Sparse) key domain and its estimates

`GetNPForEvk(-1)` returns the short base with `num_aux = num_main + num_ter`
(`UserInterface.cu:648-657`), so the DtS key is published on a small basis, not
on Q_key P or Qover:

- 32-bit: terminal[0..1] (30539777 * 37224449 = 50.013933 bits) times
  aux[0..1] (61.999560 bits) = 112.01349303022309.
- 64-bit: main[0] (50.0 bits) times aux[0] (61.0 bits) = 111.00000000151024.

Sparse support is stride 16, i.e. effective dimension N/16 = 4096
(`UserInterface.cu:365-366`); the OverModRaise layout requires weight 256
(`UserInterface.cu:146-148`; JSON `sparse_hamming_weight: 256`). The inflated
`kSubringEntryStandardDeviation = 819.2` is applied only to the DtS key
(`include/UserInterface.h:162-163`: kErrorStandardDeviation 3.2 /
kSubringEntryStandardDeviation 819.2; `UserInterface.cu:464-469`); dense keys
keep sigma 3.2. Full-ring i.i.d. errors under a subring secret decompose into
16 independent degree-4096 samples at the same modulus, so n=4096 with m
unlimited is the conservative model used below.

Reference facts and Cheddar divergence: the Go reference fixes
`SubringSecretProfile{32, 16}` (`meboot/internal/refresh/auxiliary_keys.go:82`)
and requires the DtS secret to be `ring.Ternary{H=32, ExtDegree=16}` with DtS
error sigma equal to `rlwe.DefaultNoise` (3.2) and bound `DefaultNoiseBound`
(line 88); its exposure table maps the entry secret to
`SparseBinomial(eta=1, hw=H, n=N/Extension)`
(`meboot/bootstrap/identity19_price.go:121-123,181-184,191-198`). Cheddar's
original h=32/stride-16 matched that default but scores 76.226 / 77.984 at
Cheddar's own Q0P entry, so Cheddar moved to h=256 with DtS-only
sigma 3.2 * 256 = 819.2. That hardening has no counterpart in the reference
profile: the paper's own entry-domain analysis does not cover it, and the
h=256/sigma-819.2 rows below are the only covering evidence.

Reproducible six-attack screens (n=4096, h=256, `SparseBinomial(eta=1)`,
sigma=819.2, m unlimited, BDGL16, rev `53da598...`; raw logs
`secure-attacks-819.log` `705cdc74...`, `final-entry64-attacks.log`
`413d378d...`):

| attack | 32-bit, log2 q 112.013493 | 64-bit, log2 q 111.000000 |
| --- | --- | --- |
| usvp | 132.194553 | 133.646770 |
| bdd | 131.473926 | 132.874376 |
| dual | 132.821159 | 134.280800 |
| bdd_hybrid | 131.414325 | 132.698988 |
| dual_hybrid | 129.750228 | 130.954127 |
| bdd_mitm_hybrid | 134.294153 | 135.523015 |

Minimum: 129.750228 (32) / 130.954127 (64) -- narrow heuristic margins above
128, not a certificate. Rejected predecessors: h=32 at the old entry domains
min 76.226 (32) / 77.984 (64, at the old 106-bit Q0P); h=256 with DtS sigma
204.8 min 126.992 (32). The 64-bit entry Q0P rose from 106 to 111 bits in the
current profile, which is why the 111-bit log supersedes the earlier 106-bit
row (min 138.086).

## 5. Dense normal key basis: current Q_key P estimates

The dense screen uses the exact key basis at the ceiling,
`main[:num_main] * terminal[:num_ter] * auxiliary` from
`level_config[max_key_switch_level]` (32-bit: [52, 4] plus all 12 aux; 64-bit:
[23, 0] plus all 7 aux), n=65536, h=32768, sigma=3.2, BDGL16, rev `53da598...`.

32-bit, log2 q 1743.3121404102606 (per-run JSONs record
`profile_sha256 076cb828...`):

| attack | log2 rop |
| --- | --- |
| usvp | 130.65370714282085 |
| bdd | 130.41154336116625 |
| dual | 130.71689128269253 |
| bdd_hybrid | 130.42351267723615 |
| dual_hybrid | oo (UNAVAILABLE) |
| bdd_mitm_hybrid | INCOMPLETE (non-terminating; not a bound -- see below) |

Minimum finite row 130.41154336116625. A full dense `bdd_mitm_hybrid` run
(`--dense-mitm`) was started later for both widths as independent sidecars but
produced no result; see the dedicated paragraph below.

64-bit, log2 q 1657.0001437774185 (`final-dense-screen.log` `da14b3be...`),
five attacks done: usvp 137.960300; bdd 137.883005; dual 138.016539;
bdd_hybrid 137.886176; dual_hybrid `oo`; bdd_mitm_hybrid INCOMPLETE
(non-terminating; not a bound). Minimum finite row 137.883005.

The `dual_hybrid` `oo` rows are "no finite modeled cost", not a security bound.
Dense `bdd_mitm_hybrid` does not exist for either width and is permanently
recorded as not run. The two `--dense-mitm` sidecars of the current profiles
were stopped on 2026-10-13T17:12+08 after more than 50 minutes at 100% CPU with
no result: both were stuck in a Sage/GiNaC `Expression.sqrt` over the ~2^1743
(32-bit) / ~2^1657 (64-bit) public modulus, i.e. the estimator never reached a
cost. `final-dense32-mitm.json` (`67a8100b...`) and
`final-dense64-mitm.json` (`918d9f6d...`) record `status: INCOMPLETE`,
`security_level_claimed: null` and the termination reason; the gdb stacks are
`dense-mitm-native-stack.log` (`49521f83...`) and `dense-mitm-stack.log`
(`a8e5e35a...`). An earlier 64-bit attempt on the old basis had also run more
than 34 minutes with no result. INCOMPLETE is not a bound: the row must never
be quoted as a security level, completed by interpolation, or merged with any
other table.

For history only: the original 32-bit baseline (Q_key P 1745.415737) gave a
first cost of 130.377742 (usvp) and the original 64-bit baseline (1637.000330)
gave 139.722389 (usvp). Those rows come from an earlier driver revision
(`final-candidate/dense-attacks.log`, `dense64-attacks.log`) and are context
only; they are not on a common footing with the current tables. The reallocated
32-bit basis is 2.10 bits smaller than its baseline; the current 64-bit basis
is 20 bits larger than its baseline. The 32-bit rows inside
`final-dense-screen.log` (`da14b3be...`, min 139.424701) are on the superseded
1640.272347 pre-reallocation basis and must not be quoted for the current
profile; only that file's 64-bit rows are current.

## 6. Failure-probability model (K=64, conditional)

`tail-bound.log` (`2fd39005...`; EC21 selection,
`paper/sections/joint-selection.tex:10-26`): with X the sum of H+1 = 257
independent centered Uniform(-1/2, 1/2) coefficients, the two-sided Chernoff
bound at t* solving `(1/2)coth(t/2) - 1/t = K/n`, and whole-refresh factor
2N = 2^17 (2m = 2^18):

| K | t* | log2 bound |
| --- | --- | --- |
| 16 | 0.754 | +9.337 (vacuous) |
| 32 | 1.553 | -17.158 |
| 64 | 3.573 | -132.219624 |
| 128 | 514.0 | -1925.673 |

Zero-support `K >= (H+1)/2 = 128.5` keeps the sum inside [-1/2, 1/2]
deterministically; that is sufficient, not necessary. K=64 is below 2^-128 only
under the centered-uniform, independent-coefficient, m=2N, two-sided-tail
model. The actual lift/coefficient convention and refresh count must be matched
numerically before this is more than a conditional statistical estimate. The
current audit output (`final-modulus-audit.json` `99239b38...`) records
`evalmod_K = 64` and 11 EvalMod levels for both widths, with 32-bit EvalMod
scales around 72 bits and 64-bit around 60 bits.

## 7. Precision/latency qualification (closed)

- Final 30-sample paired run on the frozen tree
  (`final/regression/performance-summary.txt` `c936bd41...`, produced by
  `meboot_performance_test` inside the 11/11 regression; levels confirm the
  structural table: 32-bit key ceiling 20 / prefix 21 / standard CtS end 16 /
  MEBOOT CtS end 18 / outputs 2 and 4; 64-bit 22/23/18/20/4/6):

  | width | standard | MEBOOT | MEBOOT delta | gates |
  | --- | --- | --- | --- | --- |
  | 32-bit | 94.319550 ms, 134.030658 dB | 92.937150 ms, 134.033378 dB | -1.4657% | all true |
  | 64-bit | 98.690172 ms, 124.900613 dB | 96.303174 ms, 125.030294 dB | -2.4187% | all true |

  `precision_alarm_passed`, `absolute_precision_passed` and
  `basic_latency_target_passed` are `true` for both widths, and MEBOOT is
  slightly faster with slightly higher SNR than the standard path. Median
  stage times and used-key payload sizes are in the same file.
- Direct EvalMod on the current profiles (`reallocated-phi.log` `a9d91ad1...`)
  passes both widths against the 1e-8 message oracle: 32-bit 6.02598e-13,
  64-bit 7.73392e-10. (The earlier pre-reallocation 32-bit ~1e-9 reading was
  traced to real HMult rounding amplified by the double-angle chain rather
  than to the polynomial; the zero-phi contract reference is about 1e-12, so
  6.03e-13 sits at that floor.)
- Regression: `final/regression/commands.tsv` (`226d2a7e...`) records 11/11
  targets at exit 0 -- user_interface, centered_crt, mod_up_to_max,
  eval_mod_range, meboot_matrix_planner, aks_key, lcr_step, meboot_cts,
  linear_transform, boot_test, meboot_performance_test. `boot_test` passed 6
  and skipped the bootparam_30/35 profiles. Extra checks under
  `final/extra/`: `basic.log` (15/15 autonomous mod-switch operations) and
  `stc.log` (StC differential, 2/2) also pass.
- The regression ran on the tree frozen at 16:15
  (`final/regression/source.patch` `37fb9618...` / `source.sha256`
  `6722ae14...`). The only later changes are documentation text, an assertion
  added to `unittest/tools/audit_meboot_modulus.py` (`de23366c...`, whose new
  output is byte-identical to `final/extra/modulus-audit-pinned.json`
  `99239b38...`) and the new `unittest/tools/audit_meboot_crt_bounds.py`
  (`7ee8a4c4...`); no C++/CUDA/parameter revision follows the freeze.
- Superseded history: the earlier paired run (`reallocated-paired.log`
  `ab802d40...`) left 32-bit MEBOOT at 110.361075714 dB against the 128 dB
  floor (64-bit 124.830724656 / 125.049403869 dB passed the 114 dB floor). The
  3-sample candidate `aks-budget-paired.log` (`02340afa...`; 32-bit
  134.033764666 / 134.023711610 dB, 64-bit 124.757097575 / 124.857449477 dB)
  is superseded by the 30-sample run above; it is the run that traced the
  Stage-2 fixed 16-bit boost to the small-top-`qL` encoding loss, fixed by
  deriving the boost from floor(log2 qL) with a >= 46-bit floor while the
  full-CtS gain stays constant.

## 8. Assumption classes, actual-CRT-kernel mapping, unresolved models

(a) Every estimator row is a heuristic BDGL16/GSA screen for an LWE key-switch
instance; it is not a proof of RLWE hardness and does not cover circular
security. (b) Structural modulus-budget compliance is a code property and is
independent of (a); passing the audit does not imply 128-bit security. (c)
Single-digit AKS needs the Prop. 3 correctness/encoding conditions
(`interfaces.tex:112-122`, proof `appendix-interface-proofs.tex:19-30`); they
are not hardness assumptions, and the estimator does not test them. The paper
states them for a single residual representative `h_0 = (q_0-1)/2`; that
window is not the condition the shipped kernel must satisfy, so it must not be
copied over as the engineering requirement.

The shipped kernel is `LCRProjectKernel` (`src/extension/LCROperator.cu:14-77`):
it takes the centered remainder `c` mod `q_L`, maps `z_i = (s_i - c) q_L^{-1}
mod q_i`, and reconstructs with `ReconstructCenteredCRT`
(`include/common/Basic.cuh:363-430`), which uses the first three limbs for
32-bit words and the first two for 64-bit words, with `crt = q_0 q_1 (q_2)` and
`M_crt/2 = 2^74.10997548617063` (32-bit, `crt` from level-20 primes
[32899073, 33292289, 37224449]) / `2^89.0000053329652` (64-bit, level-22
[1125899908022273, 1099515691009]); the prime order everywhere is
`reverse(terminal[:n_t]) ++ main[:n_m]` (`src/core/Parameter.cu:115-116`). With
`e_1` the dropped-limb rounding allowance (`1` for 32-bit, two prefix limbs;
`0.5` for 64-bit, one limb), the kernel-level conditions checked are
`a_21 (q_0-1)/2 + a_2 e_1 + 1/2 < M_crt/2`, `L_1 (H+1) B_0 < q* Q / 2` and
`L_2 (H+1) B_1 < Q / 2` with `B_1 = a_1 B_0 + e_1`.

Numerical proxy (`final/extra/crt-bounds.json` `f93c5eac...`, produced by
`unittest/tools/audit_meboot_crt_bounds.py` `7ee8a4c4...` from the frozen
`parameters/meboot_golden_4434.bin.gz` matrices under the `Encoder::SpecialIFFT`
convention; M1 diagonal gain 0.0152964 / 0.125, M2 gain 2^23 / 0.125 from
`stage2_boost` and `ctsConst`):

| width | a_1 | a_2 | a_21 | rep. log2 | M_crt/2 log2 | margin | c_1 log2 | phase margins |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 32-bit | 3.04392 | 5.59568e8 | 2.62019e7 | 73.657099 | 74.109975 | 0.452876 | 50.619865 | 1311.76 / 1258.95 |
| 64-bit | 24.87451 | 8.33821 | 3.19061 | 50.673830 | 89.000005 | 38.326175 | 53.636596 | 1167.36 / 1104.30 |

The representative and `c_1` bounds pass, and the phase inequalities clear
their right-hand sides by more than 1100 bits, but the 32-bit representative
pass has only a 0.45-bit margin. These numbers come from a floating-point
evaluation of the golden fixture with a per-unit-diagonal numerical allowance
of 1e-12; the tool labels itself "conservative floating-point diagnostic, not
a certified interval proof". The paper baselines' own selected QP gadget
carries the same conditionality, which is why the interface gap is stated here
as a production/theorem-level proof gap rather than as a budget failure.
(d) The K=64 Chernoff bound is conditional, not a certificate. (e) The
estimator uses unbounded `DiscreteGaussian`; Cheddar's testing-only
`UserInterface` samples a rounded normal clamped at 6 sigma over `mt19937_64`
(`include/Random.h:63-80`; `UserInterface.cu:613-628`). That sampler/RNG
mismatch is a production- and theorem-level proof gap: Phase 1's accepted
input contract is the bounded-CRT interface of ADR-0004
(`docs/adr/0004-phase1-subring-secret-input-contract.md`), which records a
scope decision rather than licensing a rework of the project's RNG and
sampler infrastructure.

Unresolved: dense `bdd_mitm_hybrid` is permanently INCOMPLETE (not run, not a
bound) because the estimator does not terminate symbolically on these moduli;
`dual_hybrid` `oo` is not a bound; the CRT bounds above are a floating-point
diagnostic, not certified; the sampler/RNG mismatch; and the withdrawn
fresh-Gaussian-at-Qover model, for which no valid estimate exists (see
Section 3). Structural compliance, the heuristic estimator screens, the
conditional Chernoff bound, and precision/latency remain four separate gates.

## 9. Provenance

Snapshot root `.scratch/agent-planning/meboot-modulus-security-20261013/`;
`sha256sums.txt` lists every file. Hashes below are abbreviated to 8 hex
digits; full hashes are in the snapshot.

```
076cb828  current/bootparam_40.json                  (32-bit profile)
ffe38f41  current/bootparam_40_64bit.json            (64-bit profile)
286f5b56  current/screen_meboot_security.py          (per-attack JSON driver)
362c727c  current/reallocate_phi_budget.py           (32-bit reallocation)
af629a66  current/reallocation.log
fca3efb3  current/reallocated-build.log
a9d91ad1  current/reallocated-phi.log
ab802d40  current/reallocated-paired.log
1abbc687  current/reallocated-dense-usvp.json
73a44a5f  current/reallocated-dense-bdd.json
453589f4  current/reallocated-dense-dual.json
595a2cbe  current/reallocated-dense-bdd_hybrid.json
424275e7  current/reallocated-dense-dual_hybrid.json
f778e903  current/reallocated-dense-bdd_mitm_hybrid.json
07c2d833  current/run_reallocated_security.sh
86421996  current/aks-budget-build.log
02340afa  current/aks-budget-paired.log
99239b38  current/final-modulus-audit.json
ec56671d  current/final-run.partial.log               (frozen mid-regression)
3f9a5721  verify/verify-current-profile.log          (independent recompute)
42f2acda  current/profile-high30-before-reallocation.json  (superseded 30-bit)
da14b3be  current/final-dense-screen.log             (64-bit rows current)
705cdc74  current/secure-attacks-819.log             (entry, 32-bit)
413d378d  current/final-entry64-attacks.log          (entry, 64-bit)
2fd39005  current/tail-bound.log
706b3cf0  current/audit_meboot_modulus.py
------- final evidence (2026-10-13, at or after the 16:15 freeze) -------
99239b38  final/extra/modulus-audit-pinned.json        (byte-identical to final/regression/modulus-audit.json)
f93c5eac  final/extra/crt-bounds.json                  (Section 8 numeric proxy)
c936bd41  final/regression/performance-summary.txt     (30-sample paired run)
226d2a7e  final/regression/commands.tsv                (11/11 exit 0)
37fb9618  final/regression/source.patch                (tree frozen at 16:15)
6722ae14  final/regression/source.sha256
3e13f7f0  final/profile/source.patch                   (post-freeze; docs + audit tools only)
67a8100b  current/final-dense32-mitm.json              (bdd_mitm_hybrid INCOMPLETE)
918d9f6d  current/final-dense64-mitm.json              (bdd_mitm_hybrid INCOMPLETE)
49521f83  current/dense-mitm-native-stack.log
a8e5e35a  current/dense-mitm-stack.log
de23366c  current/audit_meboot_modulus.revde23366c.py  (assertions added after the freeze)
7ee8a4c4  current/audit_meboot_crt_bounds.py
f6afcb9e  current/operator_bounds.py                   (raw CPU proxy behind crt-bounds.json)
ca9c229f  current/operator-bounds.log
```

Superseded (rejected 31-bit variant):
`high-precision/layout-high-precision.json` `a863f2af...`,
`high-precision/high-precision-chain.json.log` `c84d28a8...`.
The screens in Sections 4-5 were produced with the `286f5b56` tool revision
(snapshot copy `current/screen_meboot_security.py`); the repo tool later moved
to `28a346df` (snapshot copy `screen_meboot_security.rev28a346df.py`), which
computes the dense basis as the max over levels at or below the ceiling instead
of the ceiling entry. On the current profiles both compute the same basis, so
the rows stand. The modulus audit tool at the freeze is
`audit_meboot_modulus.rev14bbeb1d.py`; the post-freeze revision
`revde23366c` adds assertions only and its output is byte-identical to
`final/extra/modulus-audit-pinned.json` (`99239b38...`, same hash as
`final/regression/modulus-audit.json`).
Source/reference hashes: `run_lattice_estimator.py` `3a5fb411...`;
`combine_lattice_estimates.py` `bd4b1b60...`; sibling `screen_e2_security.py`
`6435ac57...`; sibling summary `b987ffea...`; `include/UserInterface.h`
`312fd834...`; `src/UserInterface.cu` `d42d2d4d...`; `src/core/Parameter.cu`
`f03d16b0...`; `src/core/ModSwitch.cu` `ebc5acd8...`; `include/Random.h`
`8e9236e7...`. Reference repo pinned at commit
`a4415572d9557c38b92cebe0da56a35bea705c54` (clean tree at snapshot time):
`meboot/internal/refresh/auxiliary_keys.go` `54fd96df...`;
`meboot/bootstrap/identity19_price.go` `e28842ff...`.

## 10. Status of the four gates

- Structural modulus budget: closed. `final/extra/modulus-audit-pinned.json`
  (`99239b38...`) reproduces the current profiles, the key ceiling / prefix
  placement, the two-main-level CtS saving, and the option-level assertions
  that key switching stops at OverModRaise.
- Precision and latency: closed on the frozen tree. `commands.tsv` records
  11/11 targets at exit 0 and the 30-sample paired run passes every gate at
  both widths (Section 7); direct EvalMod 6.026e-13 (32) / 7.734e-10 (64).
- Heuristic lattice screens: done for the entry and dense domains except dense
  `bdd_mitm_hybrid`, which stays permanently INCOMPLETE for both widths -- not
  run, not a bound. `dual_hybrid = oo` is likewise not a bound. Entry minima
  129.750228 (32) / 130.954127 (64); dense minima 130.411543 (32) /
  137.883005 (64). These are BDGL16/GSA heuristic screens, not a 128-bit
  claim.
- Security at the theorem level: still open, and deliberately separate from
  the three gates above. The CRT interface has a floating-point diagnostic
  with a 0.45-bit 32-bit margin, not a certified interval proof; the
  estimator's unbounded discrete-Gaussian model does not match Cheddar's
  testing-only RNG; and no valid fresh-Gaussian-at-Qover estimate exists.
