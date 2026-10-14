# 06: Complete the selected ordinary StC-first identity refresh

**What to build:** A full selected ordinary input refreshes through native
ephemeral entry, three-factor CtS, Mod1, correction and bypass to a correct
level20/scale40 full-slot identity output.

**Blocked by:** 02: Reach each selected entry contract through native StC-first execution;
05: Evaluate both selected Mod1 functions with the native polynomial compiler.

**Status:** execution-in-progress (owner authorized 2026-10-13)

- [ ] Use exact ordinary Q/P, full-ring ephemeral H32 and native hybrid
  Q0/P0 entry; no joint low-q0P entry or subring restriction leaks into it.
- [ ] Reuse centered ModUp, ordinary KS and LinearTransform with
  widths[5,5,5]/bases[4096,256,8], reaching Mod1 level28.
- [ ] Confirm actual sparse baby-index/decomposition counts use compatible
  native fused kernels; canonical residue bases are not buffer counts.
- [ ] Carry an independently bound bypass and explicit correction units;
  reuse native real/imaginary split/recombine and arithmetic.
- [ ] Run through the public bootstrap/profile seam with no hot-path host
  transfer and existing fusion features enabled.
- [ ] Pass signed grid, endpoints, independent complex patterns and three
  fresh key sets with max complex error<=2^-18 and level20/scale40.
- [ ] Compare observable stage values to the reference and preserve retained
  standard bootstrap behavior.
- [ ] Record correctness, host/GPU time, key payloads and peak allocation.

## Execution evidence (2026-10-13)

Implemented; precision gate FAILED in first complete diagnostic. Level20/scale2^40 is correct, maximum complex error 4.1862273448686864e-6 >2^-18. See ../execution/native/ordinary-packed16.log. Diagnosis ongoing.

Acceptance checkboxes remain open until their complete evidence exists. See ../execution/status.md for cross-ticket qualification.

## Continuation evidence

Three fresh-key packed diagnostics pass at level20/scale2^40, maximum errors 1.2924e-6,1.2417e-6,1.8929e-6. See ../execution/native/selected-packed-three-keys.log. Public-seam exact-residue checks and separate full-grid/zero/endpoint/independent patterns are being verified. Previous failure evidence is retained.
