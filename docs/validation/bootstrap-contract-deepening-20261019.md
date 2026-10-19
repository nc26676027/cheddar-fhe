# Bootstrap Contract Deepening Validation

Date: 2026-10-19. Starting revision: 6ba649e.

## Scope

Implements the approved
[bootstrap contract specification](../specs/0005-bootstrap-contract-deepening.md)
and its [four execution tickets](../../.scratch/bootstrap-contract-deepening/README.md).

- BootTransforms owns entry, centered lift and the proper/dense fork for native
  Identity19 and Discrete MEBOOT. Ordinary keeps its distinct entry switching.
- DiscreteIdentityExecution owns discrete argument alignment, actual cleaner
  depth, bypass conversion and final normalization.
- Both BootContext creation overloads share core-parameter validation.
- Private BootstrapTiming owns event lifetime and timing mechanics.
- Detailed baseline and MEBOOT research observations adapt to the same timing
  owner while preserving their independent stage meanings.

## Environment and Commands

Ubuntu under WSL on Windows; NVIDIA GeForce RTX 5060 Ti, 16 GiB; CUDA 12.8;
Release build with CUDA architecture 120. Tests run serially on the GPU.

Commands below run from the repository root inside Ubuntu:

```bash
cmake -S . -B build -DCMAKE_CUDA_ARCHITECTURES=120 \
  -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
cmake --build build -j 4

env MEBOOT_CENTERED_MODUP=1 \
    DISCRETE_WARMUPS=0 DISCRETE_REPETITIONS=1 DISCRETE_PROFILE_REPETITIONS=1 \
  ctest --test-dir build -j 1 --timeout 480 --output-on-failure \
    --output-junit architecture-deepening-20261019/production-regression.xml \
    -R '^(user_interface_test|centered_crt_test|mod_up_to_max_test|low_q0p_entry_test|linear_transform_test|advanced_bootstrap_test|bootstrap_contract_test|bootstrap_timing_test|discrete_integer_cleaner_test|meboot_test|meboot_discrete_identity_test)$'

# After research migration, rebuild and qualify both detailed consumers.
cmake --build build -j 4
env MEBOOT_CENTERED_MODUP=1 \
    DISCRETE_WARMUPS=0 DISCRETE_REPETITIONS=1 DISCRETE_PROFILE_REPETITIONS=1 \
  ctest --test-dir build -j 1 --timeout 480 --output-on-failure \
    --output-junit "$PWD/build/architecture-deepening-20261019/research-regression.xml" \
    -R '^(discrete_identity_baseline_test|meboot_discrete_identity_test|bootstrap_timing_test)$'

# Exercise the non-fused bypass alignment with a fixed L19 expectation.
env DISCRETE_ALIGNMENT_LEVEL=28 DISCRETE_EXPECTED_OUTPUT_LEVEL=19 \
    DISCRETE_WARMUPS=0 DISCRETE_REPETITIONS=1 DISCRETE_PROFILE_REPETITIONS=1 \
  build/unittest/meboot_discrete_identity_test \
    --gtest_output="xml:$PWD/build/architecture-deepening-20261019/research-alignment28.xml"

# Retain the existing non-affine d95 research configuration.
env DISCRETE_LEGACY_D95=1 DISCRETE_EXPECTED_OUTPUT_LEVEL=19 \
    DISCRETE_WARMUPS=0 DISCRETE_REPETITIONS=1 DISCRETE_PROFILE_REPETITIONS=1 \
  build/unittest/meboot_discrete_identity_test \
    --gtest_output="xml:$PWD/build/architecture-deepening-20261019/research-legacy-d95.xml"
```

Raw local logs and XML are in `build/architecture-deepening-20261019`.
The final changed source hashes are recorded in `final-source-sha256.json`.
The complete CTest outputs are retained in `production-details.log` and
`research-details.log`; each extra research configuration has its own log/XML.

## Results

| Validation | Result |
| --- | --- |
| Rebuild before refactoring | Passed |
| Baseline Identity19 encrypted diagnostic | Passed, 90.477 s |
| Baseline Discrete MEBOOT full-slot test | Passed, 79.814 s |
| Shared-entry Identity19 encrypted diagnostic | Passed, 87.642 s |
| Shared-entry Discrete MEBOOT full-slot test | Passed, 78.142 s |
| Shared timing: 128 stages, empty evaluation, exception/retry | 3/3 passed |
| Corrected affine-rescale preparation fixture | Passed, 55.758 s |
| Production native/Ordinary route suite | 9/9 cases passed, 278.19 s |
| Production Discrete MEBOOT | Passed, 81.22 s |
| Complete production regression selection | 11/11 targets, 45/45 cases passed, 955.78 s |
| Complete prepared-contract suite | 7/7 cases passed, 259.72 s |
| Final full project build after research migration | Passed |
| Detailed research profiling and lifecycle selection | 3/3 targets, 5/5 cases passed, 143.23 s |
| Original discrete baseline with detailed profiling | Passed, 60.81 s; 41 stages |
| Selected discrete MEBOOT after research migration | Passed, 82.04 s; 48 detailed / 6 coarse stages |
| Remez alignment 28, non-fused bypass, fixed L19 | Passed, 73.947 s; same detailed/coarse checks |
| Existing d95 non-affine research configuration, fixed L19 | Passed, 72.832 s; same detailed/coarse checks |

The selected output contracts remain Identity19 L21, Discrete MEBOOT L20 and
Ordinary L20, all at scale 2^40. Preparation tests independently assert fixed
L20/L19 expectations for four supported discrete schedules rather than copying
the implementation's depth formula.

Production profiling checks compare decoded output with normal evaluation,
including aliased input/output, and verify stage order, finite nonnegative
durations and the sum of GPU stage intervals.

The production run returned maximum message errors of 1.25894e-6 for Ordinary,
2.24661e-6 for Identity19 and 2.07935e-6 for selected Discrete MEBOOT. All remain
below their unchanged 2^-18 message gate. The discrete integer residual was
1.28609e-11, below 2^-33.

Descriptive production profiling samples (GPU / wall milliseconds) were
284.610 / 296.251 for Ordinary, 275.497 / 284.937 for Identity19 and
676.859 / 688.791 for selected Discrete MEBOOT. A same-run unprofiled discrete
repeat measured 675.552 / 685.954. These are individual samples, not a
performance comparison.

Before refactoring, the discrete full-slot run returned a maximum message error
of 2.09863e-6 and integer residual 1.40581e-11. Its profiling sample was
626.651 / 638.514 ms. Fresh process key generation and uncontrolled system
conditions prevent attributing timing or error differences to the refactor.
The controlled comparison is normal versus profiled evaluation of the same
ciphertext with the same prepared keys within each test run.

Timing capacity grows beyond the old fixed arrays. The narrow timing test
exercises 128 transient stage names and 16 throwing evaluations followed by a
successful retry. Event handles have RAII ownership; CUDA operation failures
throw runtime errors so stack unwinding releases previously acquired events.

The original discrete baseline retained L8/scale-2^40 output and its unchanged
average >14-bit / worst >11-bit numerical gates. The observed average was
14.9199 bits and worst 12.2073 bits. Its detailed profiler returned 41 ordered
stages and exactly the same decoded result as normal evaluation with the same
keys/input. Its descriptive GPU / wall timing sample was 307.496 / 320.578 ms.

After research migration, selected Discrete MEBOOT returned maximum message
error 1.97035e-6 (18.9531 bits) and integer residual 1.73388e-11. Coarse
production profiling, detailed research profiling, normal evaluation and
profiled input/output aliasing produced exactly equal decoded outputs within
that run. All 48 detailed stage names and ordering are checked, including both
channels, six extracted digits, two cleaning iterations, combination, output
and ReleaseTemps. The baseline independently checks its 41-stage ordering for
five digits. Detailed stage host intervals retain the existing wall_ms field;
coarse stage host intervals retain host_submission_ms.

Final selected detailed profiling measured 656.432 / 666.907 ms (GPU / wall);
coarse profiling measured 659.105 / 671.183 ms. These intervals have different
observation granularity and are reported separately.

The alignment-28 Remez configuration exercised rescaled bypass alignment
instead of the selected fused binding. The post-review-fix run retained
L19/scale-2^40, maximum message error 2.03009e-6 (18.91 bits), integer
residual 1.45097e-11, the original message gates, and exact
ordinary/profiled/aliased decoded equivalence. Its detailed profiling sample
was 641.873 / 651.926 ms.

The existing d95 configuration also retained L19/scale-2^40. It passed the
same integer/message, detailed/coarse profiling and aliasing checks, with
maximum message error 2.07334e-6 (18.8796 bits) and integer residual
1.48191e-11. Its detailed profiling sample was 577.197 / 586.863 ms.

`git diff --check` passed. The selected production suite and final research
selection are the validation scope; the entire repository test inventory was
not run. No source changes followed the final successful full build.

## Code Review and Performance Follow-up

The review used `HEAD` (`6ba649e04cc01b91aa4d237330778ee4580b7ef8`) as the
fixed point. It found two P2 contract issues. The newly added `Enter` method
was public even though the specification requires the existing BootContext
seam to remain external; it is now private and available only to the two
route execution friends. The research test could also omit a fixed expected
level when overriding a schedule; custom schedules now fail unless
`DISCRETE_EXPECTED_OUTPUT_LEVEL` is supplied. The standards-only review found
no further hard violations. Repeated timing assertions in three tests remain
a low-risk test-code duplication judgement call.

The corrected source was rebuilt successfully. The smoke regression passed
`advanced_bootstrap_test` and `bootstrap_timing_test` (2/2). The corrected
alignment-28 discrete route also passed its encrypted output, profiling,
aliasing and numerical checks; its output remained L19/scale-2^40.

The post-fix benchmark used two warmups, five normal repetitions and three
profiling repetitions on the RTX 5060 Ti with the existing Release CUDA 120
build. Normal GPU time averaged 261.743 ms for selected Identity19
(standard deviation 1.129 ms) and 267.763 ms for Ordinary (standard deviation
0.541 ms). These are descriptive measurements for this environment.

| Production phase | Identity19 GPU ms | Share | Ordinary GPU ms | Share |
| --- | ---: | ---: | ---: | ---: |
| StC | 20.432 | 7.8% | 18.665 | 7.0% |
| ScaleDown | 0.014 | <0.1% | 0.020 | <0.1% |
| Entry | 0.846 | 0.3% | 0.281 | 0.1% |
| ModUpAndBypass | 4.492 | 1.7% | 4.453 | 1.7% |
| CtS | 74.695 | 28.4% | 107.343 | 40.2% |
| Split | 3.451 | 1.3% | 3.906 | 1.5% |
| Mod1 + correction, real | 49.877 | 19.0% | 37.796 | 14.1% |
| Mod1 + correction, imaginary | 49.009 | 18.6% | 37.616 | 14.1% |
| Bypass + final combine | 60.283 | 22.9% | 57.193 | 21.4% |

The code reuses Cheddar's existing high-performance primitives: staged
`LinearTransform` for StC/CtS/bypass, fused `MEBootCtS` Stage 1, `LCROperator`
for the level-conserving AKS stage, existing NTT/ModUp and key-switch
operators, and `MEBootEvalMod`/`EvalPoly` for the nonlinear correction. No
second implementation of these kernels was introduced.

The production route does not repeat the same encrypted arithmetic within one
bootstrap. The real and imaginary Mod1 passes, two SplitRealImag operations,
the bypass path, and the final combine are required by the complex CKKS
construction. The detailed observer path performs extra observation callbacks
only when research profiling is explicitly requested; `ProfileBoot` keeps the
coarse route and does not run the detailed observer path.

Complexity and optimization assessment:

- StC and Ordinary CtS are dominated by BSGS baby/giant steps, rotations,
  key switches and modulus-limb work. Their cost grows with ring degree,
  active modulus limbs and transform diagonal count. Ordinary CtS is the
  largest single phase at 107.343 ms, so reducing diagonal count, improving
  hoisting reuse, or increasing fused/packed key-switch work is the clearest
  route-level opportunity.
- Selected MEBOOT CtS is cheaper at 74.695 ms because Stage 1 uses the fused
  subring kernel and Stage 2 uses LCR/AKS; Stages 3 and 4 still use the shared
  `LinearTransform` path. Any optimization should preserve the current
  subring and modulus contracts.
- Each Mod1 correction is a degree-62 polynomial route with two double-angle
  steps and is executed once per real/imaginary channel. The two passes account
  for 37.3% of selected Identity19 GPU time and 28.2% of Ordinary time.
  Shared power/basis material would be a possible optimization only if the
  polynomial evaluator can safely share ciphertext intermediates across
  channels; the current independent channels make this a design change, not
  a safe local cleanup.
- Bypass and final combine accounts for 22.9% and 21.4% respectively. It
  already uses the shared staged transform and, for the fused discrete binding,
  folds the conversion into the last factor. The remaining cost is the
  required accurate bypass transform and complex recombination.
- Entry and ScaleDown together are below 2.1% on both routes. Reworking their
  NTT/ModUp or key-switch sequence is unlikely to improve end-to-end latency
  without changing the contract.

No performance optimization was applied based only on these samples. The
measurements identify where a future optimization experiment should start;
they do not establish a speedup or justify changing the certified parameters.

## Retained Failures and Compatibility

The baseline Ordinary diagnostic exited before encryption because it requested
GetExecution, which is restricted to Identity19. The diagnostic now obtains
the native execution through GetRouteExecution. The public restriction is
unchanged; this correction allows the existing Ordinary checks to run.

An initial new Remez preparation fixture used basis scale 2^40, an unqualified
configuration. Preparation exited with a BigInt value-out-of-range error.
The test assumption was corrected to basis 2^59
to exercise affine rescaling near the selected scale. The failed log is retained
as `contract.log`; it is excluded from passing results.

The discrete StCAndEntry timing marker retains its historical position before
the entry switch. Coarse production and detailed research intervals retain
their own meanings. No timing speedup is claimed.

Fresh processes generate fresh keys. Baseline and changed-source numerical
comparisons are descriptive; within-run evaluation/profiling equivalence uses
the same keys and input ciphertext. Selected parameter manifests, certificate
evidence and historical research evidence are unchanged.
