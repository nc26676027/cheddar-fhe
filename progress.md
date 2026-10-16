# Progress

## 2026-10-15 - Successor session

- Recovered both handoff and full original request in the isolated worktree.
- Verified codex/discrete-identity at d06720c, clean and two commits ahead.
- Original workspace has pre-existing changes and remains read-only.
- Reconciled stale phase-4/5 plan entries with committed GPU passing evidence.
- Six-bit generator uses sampled minimax, not a Remez certificate; existing
  independent exact-rational certifier is hard-coded to five bits/degree 47.
- First task: generalize certificate validation without weakening the
  2^-9 extraction gate or 2^-12 full-cell radius; measure actual GPU depth.
- Certified degree-95/six-bit profile and generated independent 100-digit
  known answers for all 64 integers at zero and both endpoint noises.
- Degree-63 sampled fit fails (bit-0 error about 0.5); degree 95 compiles to
  depth 7. Original five-bit certificate inequalities are unchanged.
- Dense scale-40 cleaner failed at 25.25 bits; scale-43 input caused power
  basis scale growth and severe extraction errors. Both logs retained.
- Fixed the unused extraction_scale contract: keep the argument/power basis
  at 2^40, return extracted bits at 2^43, clean at about 2^49 then 2^67.
  GPU six-bit cleaner passes unchanged gates at 37.20 bits, including reuse
  and aliasing. Input/output levels are L20/L8, actual rescale count 12.
- Implemented research MEBOOTDiscreteIdentity and a full-slot gate without
  changing Boot() dispatch or Q/P. Shared exact encoded LCR bound checks
  with production MEBootExecution. First integrated GPU test is active.
- Custom high CtS at L20 failed worst-case precision (14.89 bits); moving
  to L23 improved to 16.27 bits. A plaintext matrix oracle isolated the
  loss to high CtS, not shared entry/lift or the cleaned integer.
- Reused native BootTransforms::Bypass (L29->L21), with explicit physical
  gain removal and output-scale alignment at L9->L8. The full 32768-slot
  identity gate passes: worst 18.9318, median 20.819, average 20.7254 bits;
  max complex error 1.99962e-6. Input is L2/2^40, output L7/2^40.
- Added repeated no-observer evaluation, GPU/wall timers, live-allocation
  stability, whole-bootstrap aliasing, and resident evaluation-key byte
  accounting to the integrated test. Rebuilding all targets and running
  the bounded suite excluding the previously timed-out basic_test.
- Full selected regression: 15/15 CTest targets passed in 1050.18 seconds.
  The opt-in performance GTest skipped as designed; basic_test was excluded
  because its known timeout is already recorded. Native Ordinary and joint
  MEBOOT, LCR, AKS, fused/unfused prefix equivalence and both Discrete routes pass.
- Final cleanup adds a logN16 guard to the shared encoded-LCR audit and
  explicit nonfinite checks for repeated/aliased outputs. Rebuilt affected
  targets; all three cleaner GTests pass again.
- Final qualification uses three fresh processes, each with a newly generated
  keyset, three timed repetitions, live-allocation stability and an alias check.
  First keyset passes at worst 18.8975 bits. Results are streamed to
  docs/evidence/discrete-identity/meboot-discrete-tests.log.
- All three final keysets pass: worst bits 18.8975, 18.8756 and 18.9616;
  averages 20.7259, 20.7255 and 20.7273. Repeated/aliased finite outputs
  retain the bounds and tracked live allocations are stable.
- Generated meboot-discrete-summary.json from raw final logs. Nine prepared
  samples have median 370.738 ms GPU / 379.814 ms wall; earlier regression
  samples were slower, so these remain pilot measurements, not a matched
  speedup claim. Resident EVK arrays 10,497,294,336 bytes; tracked peak
  allocation 14,548,030,360 bytes.
- Final integration report includes full state/scale ledger, domain/security
  scope, failed experiments, three-keyset results, reproducible commands,
  remaining performance/memory work and the requested A-N status.

## 2026-10-15 - Session 1

- Read the complete attached task and repository agent/domain instructions.
- Applied research and planning-with-files workflows.
- Audited original branch, HEAD, status, build options and GPU runtime.
- Created the isolated codex/discrete-identity worktree from an exact source
  snapshot; original HEAD and porcelain status verified unchanged.
- Created the phase plan before implementation.
- No production source has been edited for this task.
- Next: acquire primary PDFs, run baseline gates and parallelize the kernel
  inventory, source derivation and authoritative MEBOOT document audit.

## 2026-10-15 - Continued implementation session

- Recovered the request, plan and running pre-edit regression (session 82583).
  The serialized BasicTest is still active; no pass is asserted yet.
- Acquired and hashed the university-hosted publisher Discrete CKKS PDF;
  extracted all 15 pages with pypdf and supplied the full primary source to
  the specification agent.
- Acquired the original Cheddar arXiv PDF via WSL curl with TLS verification.
- Received the kernel audit and authoritative MEBOOT source contract.
  Assigned independent precision-backend and domain/security follow-ups.
- Started a separate Release/CUDA120 CMake configure in the research worktree.
  Original dependency source trees are reused read-only.
- The untouched research snapshot built successfully with the pinned
  Release/CUDA120 configuration.
- The original BasicTest reached its 600-second per-target timeout, after
  passing multiple individual cases. Recorded as incomplete, never a pass.
  User-interface, centered-CRT, centered-raise and Low-q0P regression targets
  subsequently passed; the remaining serialized baseline suite is active.
- Implemented independent signed-digit/cleaning/recombination mathematics.
  Six reference tests pass. Generated five-bit degree-47 coefficients and
  independently checked 2,080 noisy points: maximum sampled extracted-digit
  error 0.0001920161; two-cleaning integer error about 6.9926e-13.
- Added a shared-basis EvalPolyBatch and a standalone GPU integer cleaner
  using existing Cheddar kernels. Its new encrypted known-answer test is
  built but has not yet run while the baseline GPU suite owns the device.
- A separate exact rational continuum certificate now covers every full
  input cell of the five-bit profile at radius 2^-12; the HE noise gate is
  still pending.

## 2026-10-15 - Handoff checkpoint

- Added the high-precision host encoding backend, shared `EvalPolyBatch`,
  standalone `DiscreteIntegerCleaner`, and an early
  `DiscreteIdentityBaseline` scaffold in this isolated worktree.
- High-precision encoding and plaintext reference tests pass.
- Legacy regression status is 11/12 passed; `BasicTest` reached its bounded
  timeout and is not counted as a pass.
- The encrypted cleaner test is currently failing by many orders of
  magnitude. This is an unresolved scale/noise/profile issue, not a
  correctness pass.
- `DiscreteIdentityBaseline` is an early scaffold and has not passed a full
  encrypted Identity gate. MEBOOT integration has not started.
- The next agent should diagnose the encrypted input units, polynomial target
  scale, level/depth alignment, and q-chain before changing the algorithm.
## 2026-10-15 - Discrete EvalRound library seam

- Added `DiscreteIdentityExecution` as the shared deep module for both
  ordinary and MEBOOT Discrete EvalRound routes. It owns real/imaginary
  integer cleaning, bypass alignment, subtraction, final normalization and
  output checks.
- Added `DiscreteIdentityParameters` validation for width, supported degree,
  finite coefficients, input radius, digit error and scale contract.
- Added reusable CUDA event stage profiling through `Profile()`. The MEBOOT
  test now emits complete route stage timing, total GPU/wall timing, EVK
  memory and existing correctness gates in one run.
- Bound `max_input_error` into the cleaner's ideal error ledger; it is no
  longer accepted as unused metadata.
- Kept `BootContext::Boot()` and native Identity19 dispatch unchanged.
  ADR-0009 records the discrete extension seam and the adapter relationship
  between ordinary and MEBOOT CtS.

## 2026-10-16 - depth/scaling fixes and fresh profiling

- Retained low CtS's upper main-Q segment, fused `(I+0.5)/32` into the
  physical scale alignment, and added an explicit normalized cleaner input.
- Prepared per-iteration rescale counts now determine cleaner levels,
  constants, output metadata and the final normalization guard.
- Selected six-bit route: basis 2^59, extraction 2^32, two thrifty cubics
  with one rescale each. Output L18/2^40, recovering 11 levels from L7.
- Three fresh keysets and 90 timed full-slot calls pass: worst 18.8501 bits,
  integer residual at most 3.21876e-12, GPU median 524.5965 ms and host
  median 517.885 ms.
- The L9 alternative (basis 2^40, extraction 2^29, one/two rescales) also
  passes three fresh keysets/90 calls: GPU median 360.9045 ms, worst
  18.8770 bits. The preserved L7 route measures 343.282 ms. Retaining more
  RNS limbs increases latency; these are different-output-level comparisons.
- The L10/2^26 candidate fails complete identity despite passing its
  all-cell test. Upper-segment 2^33/2^34 with one/two rescales also fail.
  Raw failed trials are retained and excluded from accepted measurements.
- Four cleaner tests pass in `selected-cleaner-final.log`, including the
  original five/six-bit entries and the new all-cell normalized schedule.
- Added fifteen instrumented stage samples per new configuration; most
  L18 cost is DigitExtract (246.014 ms) and Cleaning (118.381 ms).
- Fresh exact-rational coefficient certification and public-domain audits
  pass. Current runtime fixtures match parsed source JSON exactly; source,
  binary and log hashes are recorded in the campaign directory.
- Regression complete: twelve inherited CTest targets pass initially.
  The native target hit the initial 240-second limit, then passes all nine
  cases in a separate full-target retry (281.92 seconds, effective timeout
  480 seconds). Together all thirteen inherited targets are verified.
  The known preexisting basic_test timeout remains excluded.
- Report: `docs/research/discrete-identity/depth-scaling-20261016.md`;
  raw evidence: `docs/evidence/discrete-identity/depth-20261016/`.
