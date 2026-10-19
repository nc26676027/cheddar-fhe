# Bootstrap Contract Deepening: Execution

Started 2026-10-19 from 6ba649e. The specification, ticket breakdown and test
seams were approved before implementation.

## Environment

- Host: Windows; implementation workspace shared with Ubuntu through WSL.
- CUDA compiler: CUDA 12.8; GPU: NVIDIA GeForce RTX 5060 Ti, 16 GiB.
- Build: existing Release configuration, CUDA architecture 120.
- Logs: build/architecture-deepening-20261019.
- Existing historical research evidence remains unchanged.

## Plan and Progress

| Ticket | State | Verification |
| --- | --- | --- |
| 01 Shared entry | Completed | Native, discrete and Ordinary production regressions passed |
| 02 Discrete Scaling Binding | Completed | Combined suite 7/7; selected, alignment-28 and d95 encrypted checks passed |
| 03 Production profiling | Completed | Three route/output profiling checks and 3/3 timing lifecycle cases passed |
| 04 Research profiling | Completed | Final build, 3/3 research targets and both extra research configurations passed |

The shared entry implementation deepens BootTransforms. Discrete preparation
and alignment are owned by DiscreteIdentityExecution. Timing mechanics live in
a private implementation; research migration follows production qualification.

## Evidence Policy

Record exact commands, configurations and outcomes after each validation.
Freshly generated keys differ across processes; cross-process numerical and
timing comparisons are descriptive unless a matched keyset is actually used.
No selected parameters or acceptance thresholds are changed.

## Baseline and Entry Results

- Baseline rebuild passed with the current source tree.
- Baseline native JointRefreshDiagnostic passed (90.477 seconds).
- Baseline Discrete MEBOOT passed (79.814 seconds), including the selected
  L20 result, integer error, repeated evaluation and aliasing gates.
- Baseline OrdinaryRefreshDiagnostic exited before evaluation because the
  diagnostic used GetExecution, which intentionally rejects Ordinary. The
  diagnostic now accesses the native execution through GetRouteExecution;
  the public GetExecution restriction is preserved.
- Entry-only build passed. JointRefreshDiagnostic passed (87.642 seconds)
  and Discrete MEBOOT passed (78.142 seconds) after shared entry adoption.
- Logs and XML are under the execution log directory; no cross-keyset
  numerical or timing improvement is claimed.

## Prepared Contract and Production Timing

- DiscreteIdentityExecution now derives the cleaner schedule and prepares the
  channel alignment, bypass conversion and final normalization together.
- Both BootContext construction overloads reuse common core checks.
- The initial contract run passed invalid schedule, route/manifest mismatch,
  insufficient bypass capacity and three legal schedule cases.
- The fourth new fixture used a previously unqualified Remez basis scale of
  2^40. Preparation failed with a BigInt value-out-of-range error. The test
  fixture was corrected to
  2^59, near the selected 1032*2^49 scale, to exercise affine rescaling without
  inventing support for a new low-basis configuration. The failed run remains
  in contract.log and is excluded from passing results.
- ProfileBoot now delegates to private BootstrapTiming. Its event ownership
  unwinds on exceptions, grows for detailed stage counts and checks CUDA
  operation results. Public result types and stage labels are preserved.
- New production route checks compare decrypted outputs from normal evaluation
  and profiling, including profiled aliasing and stage interval consistency.
- The corrected 2^59 affine-rescale preparation case passed in 55.758 seconds.
- Timing tests passed 3/3, including 128 stages, empty evaluation and 16 throwing
  callbacks followed by successful retry.
- The full project build passed. The serial production regression selection
  passed all 11 targets and 45 test cases in 955.78 seconds, including key
  modulus, centered CRT, lift, entry, all three routes, prepared contracts,
  timing lifecycle and linear transforms.
- The combined prepared-contract suite passed all seven cases in 259.72
  seconds; all four corrected legal configurations and all rejection cases
  are included in this clean rerun.
- Full production test output was retained as production-details.log before
  subsequent CTest runs overwrite the temporary log.

## Research Migration and Qualification

- Both existing research Profile methods now adapt detailed observations to
  BootstrapTiming. Their duplicate event arrays, synchronization and elapsed
  duration loops are removed.
- The final full project build passed.
- Final research regression passed 3/3 targets, 5/5 cases in 143.23 seconds.
  Baseline passed in 60.81 seconds and selected MEBOOT in 82.04 seconds;
  the timing lifecycle suite also passed again.
- Baseline preserves L8/scale-2^40 with 41 ordered detailed stages. Selected
  MEBOOT preserves L20/scale-2^40 with 48 detailed / 6 coarse stages.
  Research profiling equals normal decoded output; MEBOOT additionally
  profiles aliased input/output.
- The alignment-28 Remez configuration passed in 73.947 seconds, independently
  asserting L19/scale-2^40 and exercising non-fused bypass alignment with the
  unchanged integer and message gates.
- Rich CTest output is saved as research-details.log. Direct research
  configuration runs have separate log and XML files.
- Existing non-affine d95 research configuration passed in 72.832 seconds,
  with fixed L19/scale-2^40 and the same numerical/profiling/aliasing gates.
- Final changed source hashes are saved as final-source-sha256.json.
- Final diff whitespace validation passed. Parameters, certificates and
  historical evidence are unchanged. No commit or push was performed.

## Review and Performance Follow-up

- Code review fixed the public `BootTransforms::Enter` seam by making it
  private and granting access only to the two route execution classes.
- Custom discrete schedule tests now require an explicit expected output level
  instead of silently skipping that contract assertion.
- Review-fix build passed. The smoke regression passed 2/2 targets:
  `advanced_bootstrap_test` and `bootstrap_timing_test`.
- Post-fix performance runs used two warmups, five normal repetitions and
  three profiling repetitions. Selected Identity19 averaged 261.743 GPU ms
  (1.129 ms standard deviation); Ordinary averaged 267.763 GPU ms
  (0.541 ms standard deviation).
- Dominant selected Identity19 phases were CtS 74.695 ms (28.4%), bypass and
  final combine 60.283 ms (22.9%), and the two Mod1/correction passes
  49.877 ms and 49.009 ms (37.6% combined). Ordinary CtS was 107.343 ms
  (40.2%), followed by bypass/final combine 57.193 ms (21.4%).
- The implementation reuses Cheddar's LinearTransform, MEBootCtS,
  LCROperator, NTT/ModUp, key-switch and EvalPoly/MEBootEvalMod paths. No
  duplicate encrypted arithmetic was found in the production route; repeated
  real/imaginary work is required by the complex CKKS construction.
- Detailed future optimization candidates and operation-complexity reasoning
  are recorded in the validation report. No optimization was applied without
  an isolated correctness/performance experiment.
- Final review source hashes are saved as final-review-source-sha256.json.
