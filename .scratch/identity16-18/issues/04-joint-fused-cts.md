# 04: Evaluate selected joint CtS through the fused native pipeline

**What to build:** The selected proper branch executes the complete eight-term
Subring Prefix, LCR/AKS transition and dense suffix, yielding correct real and
imaginary dense CtS outputs at level 29.

**Blocked by:** 03: Produce the selected joint proper and dense bypass branches.

**Status:** execution-in-progress (owner authorized 2026-10-13)

- [ ] Bind widths[3,4,4,4]/bases[64,4], exact matrix supports and per-factor
  gains with an independent selected-profile matrix oracle.
- [ ] Specialize the existing fused prefix for eight terms, preserving its
  sixteen-term path and proving extension8 automorphism invariance.
- [ ] Verify M1 removes qExtra, M2 preserves main level31, M3 reaches30 and
  M4 reaches29; no key arithmetic occurs on qExtra.
- [ ] Establish a conservative centered coefficient bound including ModUp
  integer scale, encoded matrix norms, prefix accumulation and rounding.
  Check independent integer oracle cases around the two-limb CRT boundaries.
- [ ] Enable existing bounded/single-digit LCR/AKS only after the bound proves
  eligibility. An invalid bound blocks this ticket; document and measure any
  required narrowly scoped remedy rather than silently using a fallback.
- [ ] Reuse fused LCR accumulation, shared centered CRT, native M3/M4 double
  hoisting, baby/giant-step fusion and ModDown/rescale epilogues.
- [ ] Validate plaintext-independent CtS behavior, physical units, aliasing
  and old prefix regressions; compare fused/unfused residues where exact.

## Execution evidence (2026-10-13)

Implemented; validation in progress. Encoded-diagonal LCR bounds pass preparation. Exact eight-term prefix and independent CRT boundary tests remain to be rerun on final binary.

Acceptance checkboxes remain open until their complete evidence exists. See ../execution/status.md for cross-ticket qualification.

## Continuation evidence

Independent two-limb CRT boundaries and eight-term fused/unfused exact residues passed. Full six-prime merged ModDown/rescale BigInt oracle passed for both routes at levels 1,20,23,28,31; every coefficient and remaining Q limb was checked. See ../execution/native/centered-rescale-exact.log and deterministic-oracle-check.log.
