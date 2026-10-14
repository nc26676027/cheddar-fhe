# 07: Complete the selected joint StC-first EvalRound identity refresh

**What to build:** A full selected joint input refreshes through the native
proper branch, selected Mod1 correction and high-precision dense bypass to
a correct level20/scale40 full-slot identity output.

**Blocked by:** 04: Evaluate selected joint CtS through the fused native pipeline;
05: Evaluate both selected Mod1 functions with the native polynomial compiler.

**Status:** execution-in-progress (owner authorized 2026-10-13)

- [ ] Connect selected joint CtS and Mod1 through the public bootstrap/profile
  seam without replacing native fused operators.
- [ ] Subtract the reference integer correction with gainK*QDiff*0.25,
  matching physical units, rounding and levels before addition.
- [ ] Evaluate independently bound dense bypass CtS through native optimized
  transforms; recombine real/imaginary returns and validate output metadata.
- [ ] Validate StC gain1/64, bypass gain0.25 and CtS gain*K*QDiff=1, alongside
  complete modulus/scale transitions. Metadata changes alone are not proof.
- [ ] Pass signed grid, endpoints, independent complex patterns and three
  fresh key sets with max complex error<=2^-18 and level20/scale40.
- [ ] Keep a matched-work native-composition control, selected-profile stage
  evidence and initial timing/peak memory records.
- [ ] Retained four-bit entry and classic MEBOOT profiles continue passing.

## Execution evidence (2026-10-13)

Implemented; full encrypted joint diagnostic in progress. Not qualified.

Acceptance checkboxes remain open until their complete evidence exists. See ../execution/status.md for cross-ticket qualification.

## Exact-profile blocker (2026-10-14)

Three standalone public endpoint runs fail at 4.50e-6 to 4.93e-6 even
though their public/stage exact residues agree. The pinned Go reference,
executed natively on Windows with fresh keys, independently fails the
same endpoint pattern at 4.500504979802209e-6. Its output metadata is
level20/scale2^40. See ../execution/reference-host-joint-1.json.
Neither the fixed profile nor the 2^-18 gate has been changed.

## Continuation evidence

Three fresh-key packed diagnostics pass at level20/scale2^40, maximum errors 3.3526e-6,3.0632e-6,3.3574e-6. See ../execution/native/selected-packed-three-keys.log. Precision margin remains small; qualification still depends on public-seam standalone patterns, reference evidence and regressions.
