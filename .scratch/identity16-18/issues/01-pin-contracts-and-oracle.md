# 01: Pin selected contracts and reproduce the native baseline

**What to build:** A reproducible comparison package that identifies both exact
selected reference profiles, their arithmetic boundaries and the existing
Cheddar performance/correctness baseline before the port changes execution.

**Blocked by:** None (can start immediately after breakdown approval).

**Status:** execution-in-progress (owner authorized 2026-10-13)

- [ ] Export constructor-native Q/P arrays, qExtra, distributions, entry basis,
  polynomial coefficients/interval/offset, requested/actual degree and QDiff
  with reference revision and content hashes.
- [ ] Resolve per-stage physical basis, scales, gains, divisors, secret roles,
  rounding and output units for joint and ordinary, including absent overrides.
- [ ] Produce selected-profile signed-grid and stage oracle data with separate
  provenance; preserve existing Golden fixture identities.
- [ ] Capture the three fresh-key-set reference correctness results with the
  reference precision definition and level20/scale40 contract. Incomplete
  historical timing/progress files are not treated as acceptance evidence.
- [ ] Reproduce retained 32/64-bit bootstrap and 64-bit low-q0P baseline
  timing/precision using serial GPU work, source hashes and raw samples.
- [ ] Audit actual Cheddar key formats and estimate per-route peak-live memory
  before allocating the selected profiles, distinguishing payload from peak.
- [ ] Confirm the public bootstrap/profile seam and performance methodology
  specified for the port.

## Execution evidence (2026-10-13)

In progress. Pinned profile re-export matches SHA256; independent Go stage oracle exported. Retained baseline captured for one round; three reference key sets and three paired timing rounds remain.

Acceptance checkboxes remain open until their complete evidence exists. See ../execution/status.md for cross-ticket qualification.

## Reference execution (2026-10-14)

Native Windows execution avoids the previous WSL memory ceiling. One
fresh joint reference key set has completed; the endpoint pattern fails
at 4.500504979802209e-6, with correct level20/scale2^40. Remaining
patterns pass. The three-key reference requirement remains incomplete.
The checkout remains clean at the pinned commit. Source/binary identity
and results are retained in ../execution/reference-host-joint-1.* and
reference-windows-build.log.

## Continuation evidence

Pinned reference HEAD remains 6330f64b178b6215edd35baf5b11c8da558ca95c. Sorted-support deterministic stage oracle re-export SHA256 is 0c5159ed7355ca65e4dee9f7ae47d43b1ff2e4a19c9760e3f1da75273c176e96. All selected matrix comparisons passed in ../execution/native/deterministic-matrix-oracle.log. Reference encrypted execution still blocked by WSL key-generation RAM.
