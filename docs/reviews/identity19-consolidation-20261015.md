# Identity19 consolidation review (2026-10-15)

Scope: two-axis review of the selected architecture and its frozen contract.
ADR-0008 is the controlling decision. The consolidated implementation was
built and its nine-test GPU regression and public-interface timing run were
executed; the evidence audit below also checks the current profile binding.

## Standards findings

1. **Resolved P1 - Competing production seams.** `BootContext` now exposes one
   `Create/Prepare/AddRequiredRotations/Boot` seam. The classic
   `EvalSpecialFFT` route and selected-only entry were deleted; Cheddar's
   `LinearTransform`, `MEBootCtS`, NTT, LCR, AKS and fused kernels remain behind
   that seam.
2. **Resolved P1 - Fixture reproducibility.** The pinned reference is under
   `parameters/reference/`, the compressed oracle is under
   `unittest/fixtures/`, and the regression requires the oracle.
3. **Resolved P2 - Test surface leakage for production callers.** Performance
   uses public `BootContext::Boot`; stage observation remains diagnostic-only.
4. **Resolved P2 - Incomplete core parameter check.** `BootContext::Create`
   now rejects mismatched terminal/entry primes, main Q/P, level topology,
   key-switch ceiling, secret weights and application scale before preparing
   transforms. Regression cases cover the missing entry and wrong prefix.
5. **Resolved P2 - Diagnostic defaults and obsolete interfaces.** Stage analysis
   now uses the current route JSON. The unused CtS split wrapper, duplicate
   explicit-key-map full evaluator, constant-false key-generation branch and
   old selected reference refresh runner were removed.

## Specification findings

1. **P1 - Three-keyset qualification remains open.** The consolidated public
   seam now has nine passing tests and 30 timed calls on one RTX 5060 Ti.
   Results, levels, precision and memory are under `docs/evidence/identity19/`.
   The run used one prepared keyset; three independently generated keysets and
   a paired baseline are still required before a stronger qualification claim.
2. **Resolved P1 - Preserve route-specific levels.** Joint takes L2/scale 2^40 to
   L21/scale 2^40 using HW40, extension 8, q0 at 42 bits, Low-q0P, K20,
   actual degree 62 and DA2. Ordinary must retain K16, actual degree 30,
   DA3 and output L20. Output levels are derived independently for each route.
   Evidence: `docs/research/identity19-verification-20261015.md`.
3. **Resolved P2 - Rebind security evidence without renaming upstream cases.** Entry and main
   classical estimates are 136.3798407 and 132.4669487 bits under the
   recorded model. `docs/evidence/identity19/security.json` checks the exact
   current Q/P and entry inputs against the immutable reference estimates.
   Domain escape and numerical error have separate assumptions and gates.
4. **Resolved P2 - Historical measurements are easy to misapply.** Phase 1 HW256,
   extension 16, K64 and old Golden reports describe another entry domain.
   ADR-0001 through ADR-0007 and specs 0001 through 0004 remain dated
   historical records; ADR-0008 controls current production behavior.
5. **Resolved P2 - Qualification labels exceeded available evidence.** Parameter
   metadata now explicitly records single-keyset checks and pending independent
   keysets/paired baseline. Ordinary has no consolidated timing qualification.

## Acceptance handoff

The baseline files in `docs/evidence/identity19-baseline/` remain labeled raw
evidence. The consolidated run is in `docs/evidence/identity19/`; extend it
with the three-keyset and paired-baseline runs before changing the qualification
status.

## Workspace cleanup limit

Retired production sources, duplicate runners and stale test executables were
removed. Automatic approval review rejected recursive deletion of the ignored
historical diagnostic directories (`.scratch/identity16-18/execution`,
`.scratch/low-q0p-entry`, `.scratch/meboot-performance`,
`.scratch/phase1-acceptance`) and retired CMake target caches, returning only
`blocked by policy`. These generated directories remain outside the active
build/test inputs; no bypass was attempted.
