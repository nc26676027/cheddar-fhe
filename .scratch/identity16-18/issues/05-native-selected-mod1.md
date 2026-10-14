# 05: Evaluate both selected Mod1 functions with the native polynomial compiler

**What to build:** Prepared ciphertexts satisfying each selected Mod1 input
contract can be evaluated with Cheddar's native polynomial/multiply pipeline
and return the reference nonlinear values and output scale/level.

**Blocked by:** 01: Pin selected contracts and reproduce the native baseline.

**Status:** execution-in-progress (owner authorized 2026-10-13)

- [ ] Bind exact joint degree62/K27 and ordinary degree30/K16 CosDiscrete
  coefficients, intervals, offsets, QDiff and gain0.25.
- [ ] Use native EvalPoly/BasisMap/AXYPBZ and multiply/relinearize/rescale,
  keeping the compiled GPU evaluation schedule instead of translating the
  reference evaluator's allocation graph.
- [ ] Joint starts at29 and consumes9 levels; ordinary starts at28 and
  consumes8. Both use DA3/logscale58/log-message-ratio10 and no inverse poly.
- [ ] Represent joint's value-preserving power-basis x2 scale lift explicitly;
  ordinary has no lift. Derive all basis scales and rounding from the oracle.
- [ ] Keep nonzero coefficients unless an independently checked approximation
  budget justifies pruning; do not inherit the default1e-9 cutoff blindly.
- [ ] Check scalar and encrypted interval/endpoints/integer-neighborhood
  behavior at reference rounding tolerances and preserve original EvalMod
  profiles.
- [ ] Provide a reproducible native-composition control and stage timings
  before introducing additional expression fusion.

## Execution evidence (2026-10-13)

Isolated encrypted tests passed on initial binary. Joint degree62 and ordinary degree30 maximum polynomial error <2^-18. Final binary and independent stage comparison remain.

Acceptance checkboxes remain open until their complete evidence exists. See ../execution/status.md for cross-ticket qualification.

## Continuation evidence

Selected-only centered CRT quotient correction retains the merged epilogue. Same-input/same-key merged and composed Mod1 decoded results agree; isolated errors 5.93e-9 joint and 3.31e-9 ordinary. See ../execution/native/mod1-centered-merged-v2.log. Nearest encoding now applies to selected coefficient/scalar constants; final repeat underway.
