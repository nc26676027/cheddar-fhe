> Historical specification (2026-10-15): this records earlier design scope. ADR-0008 defines the current production Identity19 and Ordinary routes; previous classic and Phase 1 paths are superseded.

# Identity16-18 native Cheddar port

Owner update on 2026-10-14: joint Scaling Binding may be retuned to complete
the unchanged precision gate. Preserve q0 and the complete Q/P/entry chains,
18 application levels and level20/scale2^40 output. Ordinary retains the
reference binding. See ADR-0007's joint scaling update; statements below about
exact reference scaling apply subject to this explicit joint exception.

Status: draft for seam and ticket review.
Date: 2026-10-13.
Owner decisions: StC-first EvalRound included; joint4434 and ordinary included;
64-bit first; correctness before security screening; preserve native GPU
optimizations and reuse Cheddar components as far as their contracts allow.

## Problem Statement

The integrated four-bit Low-q0P Entry Gadget still runs with the retained
Cheddar bootstrap profile. It does not implement the selected MEBOOT
Identity16-18 execution, output-depth, modulus-chain, or scaling contracts.
The owner needs a faithful GPU implementation of both selected routes and
reviewable evidence that the port preserves correctness and GPU performance.

## Solution

Provide independent 64-bit selected joint4434 and ordinary identity refresh
profiles using StC-first EvalRound, exact route-specific Q32/P5 arrays,
reference scaling bindings and output level 20 / scale 2^40. Execute through
Cheddar's native transforms, hoisting, NTT, modular arithmetic, polynomial
compiler, memory manager and fused kernels. Retain existing qualified profiles
and measure them for regressions.

Qualify correctness, peak memory and matched-work latency before presenting
the new route as performance-preserving. Failed or inconclusive gates remain
visible in the result artifact.

## User Stories

1. As an FHE researcher, I want the selected joint profile imported exactly,
   so that GPU results correspond to Identity16-18 rather than historical A63.
2. As an FHE researcher, I want the selected ordinary profile imported
   independently, so that the baseline keeps its own algorithm and parameters.
3. As an application developer, I want full-slot level-2 inputs at scale 2^40
   refreshed to level 20 at scale 2^40, so that 18 usable levels are available.
4. As an FHE researcher, I want StC-first EvalRound including the dense bypass,
   so that the high-precision identity return matches the selected algorithm.
5. As an FHE researcher, I want route-bound entry distributions and bases,
   so that changing H or error sigma does not silently modify old experiments.
6. As a maintainer, I want scaling binding checked at arithmetic boundaries,
   so that incorrect gains cannot be hidden by ciphertext metadata changes.
7. As a GPU developer, I want the existing fused Subring Prefix generalized
   to eight terms, so that the selected extension-8 path avoids intermediate
   rotation buffers.
8. As a GPU developer, I want the bounded LCR fast path used only on eligible
   coefficients, so that its speed does not depend on an unverified CRT lift.
9. As a GPU developer, I want native double hoisting and ModDown/rescale
   epilogues on dense transforms, so that the port preserves memory efficiency.
10. As a GPU developer, I want selected CosDiscrete polynomials evaluated by
    the native compiler, so that CPU-reference allocation patterns do not
    dictate GPU execution.
11. As a benchmark reader, I want host/GPU latency, precision, depth and memory
    reported together, so that speedups are compared on explicit contracts.
12. As a benchmark reader, I want matched-work native controls, so that any
    loss introduced by fusion or scheduling can be separated from more depth.
13. As a maintainer, I want existing 32/64-bit routes preserved and retested,
    so that the experimental port does not invalidate qualified behavior.
14. As a reproducibility reviewer, I want exact profiles, source revisions,
    stage fixtures and raw results, so that I can reproduce every claim.
15. As an operator of a 16 GiB GPU, I want a pre-allocation live-memory plan,
    so that exact profiles can run without silently weakening parameters.
16. As an FHE researcher, I want failures and unresolved performance gates
    reported as such, so that an experiment is not mistaken for qualification.

## Implementation Decisions

- Add an explicit selected identity execution plan beside the retained classic
  plan. Plan boundaries carry physical basis, level, ciphertext scale, transform
  gain, secret identity, divisor and output units. Preparation validates the
  plan once; execution consumes prepared objects.
- Keep Parameter/NPInfo for ordered Q32/P5 chains. Joint reserves an independent
  no-P qExtra above the main key ceiling; ordinary has no proper qExtra.
  Final application scale is 2^40 even though StC joint's intermediate output
  is 2^32. Ordinary zero overrides mean "derive default".
- Bind selected input/output scales explicitly rather than inheriting
  Parameter's square-root default scale recurrence through the low StC primes.
  Existing per-level default scales retain their semantics.
- Bind joint entry Q0=4398044938241, Pentry=786433, four-bit decomposition,
  subring H=40/extension=8 and full-ring entry error sigma=3.2. Ordinary uses
  its native full-ring H=32 ephemeral/hybrid entry. Dense H=2048 for both.
- Joint uses eight-term M1, level-conserved M2 at the main ceiling, and native
  M3/M4 dense transforms with widths [3,4,4,4] / bases [64,4]. Ordinary uses
  three native factors [5,5,5] / bases [4096,256,8].
- Import exact selected CosDiscrete coefficients, intervals and QDiff.
  Joint K=27/degree62/DA3/LogScale58/LogMessageRatio10/gain0.25/basis-lift1;
  ordinary K=16/degree30/DA3/LogScale58/LogMessageRatio10/gain0.25/no basis lift.
  Both omit the inverse polynomial.
- Reuse EvalPoly/BasisMap/AXYPBZ for nonlinear work, with only the scale
  support needed for the selected polynomial schedule. Derive any coefficient
  pruning from an error budget; preserve nonzero coefficients by default.
- Reuse native StC/CtS matrices, StripedMatrix, LinearTransform and
  HoistHandler with explicit per-factor scales. Keep compact plaintext support,
  merged products, fused ModDown/rescale, baby-step and giant-step accumulation.
  Verify actual selected support/decomposition counts activate compatible
  fused specializations; enabled flags alone are not evidence of execution.
- Keep both proper and bypass device-resident; preserve the subring component
  used by M2 across dense bypass KS. Reuse native centered ModUp and normal KS.
  Shared scratch must not alias a live branch.
- Prove the selected M1/M2 components fit the existing two-limb bounded CRT
  range before enabling the single-digit AKS fast path. An ineligible bound
  blocks qualification and requires a separately measured remedy.
- Scaling binding is mandatory irrespective of Grafting. Ordered single-prime
  chains avoid requiring a new mixed-base rational schedule here; existing
  Cheddar rational rescaling remains supported. Do not claim Grafting and LCR
  are equivalent.
- Budget actual Cheddar key/diagonal/scratch ownership before GPU allocation;
  prepare the two routes serially. Do not relax chains/depth/precision to fit.
- Preserve old defaults, fixtures and their provenance. Explicit selected
  profiles narrow new behavior rather than broadening old assertions globally.
- Benchmark performance preservation on matched-work controls and old-route
  before/after baselines. Selected joint/ordinary comparison is a separate
  common-output-contract comparison; their ciphertext bases differ.

## Testing Decisions

- Preferred highest seam: the existing public BootContext bootstrap entry
  with an explicit selected profile and its existing profiling facility.
  Add route selection to that seam rather than exposing a general reference
  evaluator graph. The owner reviews this seam with the ticket breakdown.
- Test observable encrypted behavior: expected identity values, correct output
  basis/level/scale/slot count, rejected incompatible inputs/keys and preserved
  old profiles. Stage probes reuse existing isolated transform, entry and
  polynomial seams; downloads are diagnostic-only.
- Reference qualification: full-slot signed grid in [-0.25,0.25], imaginary
  coordinate minus the real coordinate, plus zero, endpoints, independent
  real/imaginary patterns and at least three fresh key sets. Both selected
  routes must have finite maximum complex error <=2^-18 and return level20 /
  scale2^40; report SNR additionally rather than substituting it.
- Golden tests have new selected-profile provenance, including exact arrays,
  matrix supports, scales, polynomial interval/offset, qExtra and source hashes.
  Compare decoded values for independently randomized CKKS executions.
  Exact residue comparisons apply to fused/unfused operators sharing identical
  inputs and keys when the arithmetic order is provably exact.
- Check entry reconstruction/round trip, prefix automorphism invariance, LCR
  coefficient bounds and centered projection, Mod1 interval endpoints/integer
  neighborhoods, correction sign and bypass units before full timing.
- Correctness passes before performance. Five warmups, >=30 calls per route
  per run, three independent paired runs, alternating order and serial GPU
  execution; report host and CUDA event median/p95 and paired 95% confidence
  intervals. No per-stage synchronization in the production path.
- Proposed no-loss gate: optimized/control and retained-route after/before
  median ratios <=1.00, and 95% upper confidence bounds <=1.03 for both clocks.
  The 3% limit represents measurement uncertainty, not an allowed measured
  slowdown. Inconclusive results require additional sampling.
- Selected joint <= selected ordinary latency is the integration performance
  target. Failure blocks a performance-preserving claim. Do not change the
  algorithm to improve this comparison.
- Record preparation/key generation separately, steady-state and peak-live
  GPU memory, per-stage intervals, used/provisioned key payloads, launch/transfer
  observations where tooling supports them, raw samples and source identities.
- Run existing basic RNS/NTT/rescale/KS tests and retained 32/64-bit bootstrap
  regressions. Required native fusion features remain enabled.

## Out of Scope

32-bit Identity16 port, new security claims or production-profile replacement,
ring dimension drop, CNN/nonlinear functional activation routes, changing
reference parameters to improve speed, CPU-reference timings as a GPU baseline,
general arbitrary full-Q LCR support, wholesale backend replacement, and
removing Cheddar's rational rescaling/Grafting-related support.

## Further Notes

The algorithm choices are accepted by the owner. The testing seam, numerical
performance gate and ticket decomposition are proposed for review, not already
approved. This task produces research, a specification and execution tickets;
it does not execute the port.

GitHub Issues are disabled on the configured repository. Keep reviewable local
artifacts without changing repository settings; publish to an enabled tracker
only after the ticket breakdown is approved.

The accepted selected-profile architecture warrants a scoped ADR alongside
the old OverModRaise/Subring Prefix decisions. It does not supersede the old
profiles' experimental numerical or security qualifications.
