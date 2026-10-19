# Spec: Bootstrap Contract Deepening

**Status:** ready-for-agent
**Approved:** 2026-10-19; user confirmed the testing seams, ticket scope and dependencies.
**Date:** 2026-10-19
**Source revision:** 6ba649e
**Delivery:** Local tracker fallback because the GitHub CLI is unavailable.

## Problem Statement

Contributors extending Identity19 and Discrete EvalRound must maintain the same
MEBOOT entry and centered-lift coordination in two route adapters. The physical
gain, ciphertext scale, shared lifted integer and normal key-switch modulus
ceiling must remain consistent across both implementations.

Discrete route preparation also distributes Scaling Binding knowledge across
the native profile, cleaner, bypass and final normalization. The native profile's
L21 describes bypass capacity; the selected discrete route returns L20 after its
own cleaning and normalization. These are different contracts, but their
ownership is difficult to follow and tests reconstruct the schedule.

Production and research profiling repeat CUDA event ownership, synchronization
and elapsed-time calculations. They observe different stage granularities,
which must remain explicit when their timing implementation is consolidated.

These three areas reduce locality and require callers to understand more of the
implementation than the existing bootstrap interface should expose.

## Solution

Deepen the existing bootstrap implementation in three areas:

1. Concentrate MEBOOT entry, centered lift and preparation of the proper and
   dense paths in one internal module used by both MEBOOT route adapters.
2. Concentrate discrete schedule validation and Scaling Binding derivation in
   one prepared contract owned by the discrete module.
3. Use one internal timing implementation for production and research profiling,
   preserving each mode's stage meaning and existing public results.

The result gives contributors one place to change each contract and lets callers
continue using the established bootstrap seam. Each change ships with observable
behavior checks and preserves the selected encrypted output contracts.

## User Stories

1. As a bootstrap contributor, I want Identity19 and Discrete MEBOOT to share
   entry coordination, so that an entry correction applies to both routes.
2. As a bootstrap contributor, I want centered lift and its physical gain
   binding owned together, so that scale relabeling cannot drift from arithmetic.
3. As a bootstrap contributor, I want the proper and dense paths derived from
   the same lifted ciphertext, so that both branches retain the same integer.
4. As a library caller, I want BootContext to remain the production seam, so that
   internal consolidation does not add orchestration to my application.
5. As a key-preparation caller, I want the current UserInterface workflow
   preserved, so that evaluation continues to use the existing native key maps.
6. As an Ordinary route caller, I want its full-ring entry contract preserved,
   so that a MEBOOT refactor does not change the comparison route.
7. As a discrete-route contributor, I want one owner of the prepared Scaling
   Binding, so that cleaner, bypass and normalization agree before evaluation.
8. As a caller inspecting the discrete route, I want its final output contract
   distinguished from native bypass capacity, so that I use the correct level.
9. As a researcher selecting a supported cleaner schedule, I want its actual
   depth used in preparation, so that a second depth formula cannot diverge.
10. As a caller supplying extractor coefficients, I want the manifest to remain
    explicit input, so that library execution does not depend on a hidden path.
11. As a contributor validating parameters, I want unsupported combinations
    rejected during preparation, so that an invalid route cannot evaluate.
12. As a numerical researcher, I want coefficient validation distinguished from
    encrypted certification, so that structural checks do not overstate evidence.
13. As a test maintainer, I want fixed expected contracts for selected routes,
    so that tests detect an incorrect production depth calculation.
14. As a test maintainer, I want tests to cross existing module interfaces,
    so that internal CUDA and transform refactors do not rewrite the test suite.
15. As a performance researcher, I want one owner of CUDA event lifetime and
    timing calculations, so that profiler fixes apply consistently.
16. As a production profiler caller, I want existing coarse stage names and
    result fields preserved, so that existing performance consumers keep working.
17. As a discrete researcher, I want detailed cleaner observations preserved,
    so that per-channel and per-iteration diagnostics remain available.
18. As a profiler caller, I want resource cleanup after success and failure,
    so that repeated measurements do not accumulate profiling resources.
19. As a maintainer, I want each ticket independently verifiable, so that it can
    land without waiting for an unrelated architectural change.
20. As an implementing agent, I want explicit dependencies and completion
    criteria, so that I can select an unblocked ticket without guessing.

## Implementation Decisions

- BootContext remains the external seam. BootstrapExecution retains the native
  and discrete route adapters. Existing callers retain their creation,
  preparation, key-request, evaluation and profiling workflow.
- The shared MEBOOT entry module owns StC, ScaleDown, Low-q0P entry switching,
  centered coefficient lift, physical lift gain, scale binding and the
  restriction to the normal modulus basis before sparse-to-dense key switching.
  Its implementation produces the proper and dense branch inputs from the
  same lifted ciphertext.
- Depth comes from concentrating the complete entry-to-fork contract. A shallow
  forwarding module around ModUp does not satisfy this scope.
- The MEBOOT implementation reuses the native arithmetic and key containers.
  Ordinary keeps its distinct entry behavior. EvalMod, DigitExtract and integer
  cleaning stay with their existing route adapters.
- Entry-key requirements, modulus-ceiling checks and encoded LCR qualification
  retain their existing enforcement. Route-specific CtS gain and LCR bounds
  stay route-specific; sharing entry does not imply identical CtS matrices.
- The discrete module owns a prepared Scaling Binding for the argument map,
  alignment rescale choice, cleaner schedule, cleaner output, bypass conversion,
  normalization and final output. Derivation reuses actual cleaner depth.
- Native bypass capacity and the discrete final result remain distinct facts.
  Preserve existing public native profile behavior and core-parameter defaults;
  expose the discrete result through the existing route output interface.
- Both BootContext construction overloads share common core-parameter
  validation, while keeping route-specific parameter requirements explicit.
- The selected results remain Identity19 L21, Discrete MEBOOT L20 and Ordinary
  L20, all at scale 2^40 from the supported full-slot L2/scale-2^40 input.
  Research schedules retain their currently supported, explicitly derived
  output contracts rather than being forced to L20.
- Coefficients and manifest values remain caller-supplied. Preparation checks
  structure and arithmetic feasibility; encrypted numerical certification
  remains separate evidence attached to the selected manifest.
- One internal timing module owns event allocation, capacity handling, event
  recording, synchronization, duration conversion and cleanup. A stage-marker
  adapter and an observation adapter feed their existing stage events to it.
- Production profiling retains coarse route stages. Research profiling retains
  detailed observations. Each mode preserves its own stage names, order and
  public result fields; those intervals are not declared interchangeable.
- Host duration field meanings remain compatible with existing consumers.
  The final ReleaseTemps interval continues to include completion of the
  evaluation callable and release of its local temporaries.
- The timing migration is additive first: adopt the common implementation in
  production, then migrate research callers and remove their duplicate event
  machinery. Each ticket must compile and verify independently.
- No generic timing backend or new public stage interface is required. The
  two existing timing adapters justify an internal seam.
- ADR-0008 and ADR-0010 govern production behavior. ADR-0009's shared discrete
  cleaning design remains applicable; its superseded dispatch restriction does
  not apply. Earlier phased profiles are historical context.
- Passing the deletion test means removing duplicated coordination and
  derivation from callers. Merely copying those facts into another record adds
  no useful depth.

## Testing Decisions

- Primary seam: BootContext creation, preparation, Boot and
  ProfileBoot with real route adapters and existing native key preparation.
  Prefer this seam over exposing new production stage methods.
- Research seam: the existing discrete baseline and MEBOOT
  discrete adapter evaluation/profiling interfaces and supported observations.
  Keep these available for independent stage and common-integer evidence.
- Existing prior art includes the Identity19 exact-contract and matrix-oracle
  tests, mismatched-core-parameter rejection, the full-slot discrete
  common-integer test, the discrete baseline encrypted test and cleaner
  schedule tests.
- Use independently fixed selected expectations: Identity19 L21, Discrete
  MEBOOT L20 and Ordinary L20 at scale 2^40. Assert actual output metadata and
  decrypted behavior, rather than only comparing two production accessors.
- Preserve each route's existing numerical gates, manifests and stage-oracle
  tolerances. Compare real and imaginary outputs and retain supported input /
  output aliasing checks.
- Verify that entry consolidation preserves the shared integer in the two
  branches through existing encrypted observations. Keep normal key-switch
  ceiling rejection and Low-q0P key-contract checks.
- Cover supported discrete alignment and rescale schedules using explicit
  expected results and invalid combinations. Tests must not reproduce the
  implementation's affine or depth formula as their expected-value oracle.
- Verify production and research profiling outputs against normal evaluation,
  the expected stage order, finite nonnegative timing values and compatible
  host-duration semantics. Preserve ReleaseTemps and detailed cleaner events.
- Check timing resource cleanup for successful evaluation, an evaluation
  callable that throws and supported capacity handling. Use the internal
  timing interface only where the public bootstrap seam cannot exercise an
  event-lifecycle failure deterministically. Avoid a generic mock backend.
- Reuse the existing GPU build and test workflow for affected routes and the
  relevant core arithmetic regression tests. Each ticket records commands,
  configuration and results; blocked GPU validation remains explicitly pending.
- Use a fixed configuration and keyset for before/after numerical and
  descriptive timing comparisons. Do not add exact-latency assertions or claim
  a speedup from timing consolidation.
- Replace tests tied to deleted shallow helpers with tests through the owning
  module interface. Preserve independent mathematical and encrypted oracles.

## Out of Scope

- Changing selected moduli, secret distributions, extractor coefficients,
  certified domains, polynomial degrees or numerical acceptance thresholds.
- Changing the MEBOOT factorization, LCR arithmetic, AKS construction, NTT
  kernels, hoisting or fused arithmetic implementation.
- Folding Ordinary into the Low-q0P entry contract.
- Changing the public bootstrap workflow, moving key generation into evaluators
  or loading research manifests implicitly.
- Repeating the completed LinearTransform, evaluation-key store, modulus
  switching and four-stage CtS consolidation.
- Introducing a general-purpose profiling framework or merging coarse and
  detailed timing results into one stage taxonomy.
- New security proofs, fresh numerical certificates, or performance claims
  beyond the evidence produced by the implementation validation.

## Further Notes

This specification captures all three candidates from the 2026-10-19 architecture
review. Shared entry and discrete Scaling Binding are the higher-priority
changes; timing consolidation has smaller leverage.

The approved execution tickets are:

1. Share MEBOOT entry and centered lift across both production route adapters.
2. Concentrate prepared Discrete EvalRound Scaling Binding.
3. Adopt shared timing through production ProfileBoot.
4. Migrate detailed discrete research profiling to shared timing.

Tickets 1, 2 and 3 have no prerequisite tickets. Ticket 4 is blocked only by
ticket 3. Tickets 1 and 2 may touch the same route implementation; coordinate
integration if developed concurrently, but that overlap is not a behavioral
dependency. Recommended serial order is 1, 2, 3, 4.

Execution tickets are stored in
[the local ticket index](../../.scratch/bootstrap-contract-deepening/README.md).
The user approved the testing seams, ticket scope and dependencies on 2026-10-19.
All four local tickets are ready-for-agent; execute only tickets whose blockers
are complete. Publish the specification and tickets to GitHub if the configured
CLI becomes available.
When publishing, use native blocking links where supported and keep the parent
issue open.
