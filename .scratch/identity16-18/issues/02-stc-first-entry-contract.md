# 02: Reach each selected entry contract through native StC-first execution

**What to build:** Selected joint and ordinary inputs can use an explicit native
identity execution plan to run dense StC and ScaleDown, reaching the same
encrypted level-zero entry value and units as their reference route.

**Blocked by:** 01: Pin selected contracts and reproduce the native baseline.

**Status:** execution-in-progress (owner authorized 2026-10-13)

- [ ] Accept full-slot level2/scale40 input under each exact ordered Q32/P5
  profile; reject incompatible input metadata before evaluation.
- [ ] Bind explicit selected input/output scales without inheriting or globally
  changing Parameter's square-root default scale recurrence.
- [ ] Bind a separate selected execution plan; existing classic profiles retain
  their behavior and fusion defaults.
- [ ] Reuse native FFT matrices, StripedMatrix, LinearTransform and scalar/
  modulus operations for the two-factor StC and ScaleDown.
- [ ] Match joint intermediate37.49017137195595/output32/gain1/64 and derive
  ordinary's native defaults from the oracle rather than copying joint values.
- [ ] Verify decrypted boundary values, scale, physical basis and reference
  integer rounding on grid/endpoints and independent real/imaginary inputs.
- [ ] Preserve compact plaintext support and native ModDown/rescale epilogues;
  no intermediate device-to-host transfer in timed execution.
- [ ] Run the retained classic bootstrap regression relevant to the seam.

## Execution evidence (2026-10-13)

Implemented; numerical qualification pending. Ordinary and joint encrypted StC/ScaleDown run. Full independent matrix comparison is being checked.

Acceptance checkboxes remain open until their complete evidence exists. See ../execution/status.md for cross-ticket qualification.

## Continuation evidence

Final API policy check (2026-10-14): selected nearest rounding is enabled
by `BootContext::CreateSelected`, before any input encoding or preparation.
Both `EncoderPolicyBeforePreparation` and `CoefficientRoundingContract`
pass in `../execution/native/encoder-policy-final.log`. Retained encoders
keep their default truncation policy.

All StC matrices independently match the sorted Go oracle. Selected-only nearest coefficient rounding now matches the reference contract; exact rounding boundary test passed. Joint inverse-StC diagnostic error improved from 5.77e-6 to 2.77e-6 on different fresh keys. Retained encoding defaults are unchanged.
