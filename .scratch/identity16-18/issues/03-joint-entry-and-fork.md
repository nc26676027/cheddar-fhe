# 03: Produce the selected joint proper and dense bypass branches

**What to build:** A selected joint input reaching level zero can undergo the
four-bit Low-q0P Entry Gadget and native centered ModUp, producing independent
correct device-resident subring proper and dense bypass ciphertexts.

**Blocked by:** 02: Reach each selected entry contract through native StC-first execution.

**Status:** execution-in-progress (owner authorized 2026-10-13)

- [ ] Bind Q0=4398044938241/Pentry=786433/base-two-width4 with H40/ext8 and
  full-ring entry error sigma3.2; preserve old H256/ext16/sigma819.2 profiles.
- [ ] Use the selected 11-digit key shape derived from Q0; reject stale
  13-digit Phase 1 keys at the selected entry boundary.
- [ ] Verify exact gadget digits, noisy/noiseless entry round trip and
  evaluation-key basis rejection using the existing entry seam.
- [ ] Reuse NTT, centered single-prime lift, ordinary KS/hoisting and RMM;
  proper includes exact independent qExtra, bypass contains only main Q32.
- [ ] Apply the reference value-preserving integer scale-up toward Mod1
  scale/message ratio, accounting for residue scaling and metadata together.
- [ ] Preserve the subring a-component needed by M2 across bypass dense KS;
  aliasing and scratch reuse cannot overwrite a live branch.
- [ ] Compare decrypted proper/bypass values and all boundary metadata to the
  oracle; track their separate secret roles.
- [ ] Enforce the pre-allocation memory estimate and main-key ceiling; provide
  branch timing through existing asynchronous profiling.

## Execution evidence (2026-10-13)

Implemented; numerical qualification pending. Joint entry and resident proper/dense branches execute with extension8/H40, eleven base16 digits and sigma3.2.

Acceptance checkboxes remain open until their complete evidence exists. See ../execution/status.md for cross-ticket qualification.

## Continuation evidence

Three fresh-key packed full-refresh diagnostics pass including resident joint entry/proper/dense stages. Full standalone-pattern public seam and independent encrypted reference-stage comparisons remain in progress.
