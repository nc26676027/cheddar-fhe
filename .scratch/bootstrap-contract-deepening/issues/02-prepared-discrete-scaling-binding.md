# 02: Concentrate prepared Discrete EvalRound Scaling Binding

**What to build:** Discrete MEBOOT preparation owns a coherent Scaling Binding
for argument alignment, cleaner depth, bypass conversion and final
normalization. Callers obtain the actual discrete result contract through the
existing route interface without reconstructing the schedule. Keep native
bypass capacity distinct from the final discrete output. This concentrates
derivation for locality and gives callers leverage through the existing seam.

**Blocked by:** None (can start immediately).

**Status:** completed

**Specification:** Bootstrap Contract Deepening, prepared discrete contract
workstream.

- [x] One prepared owner derives alignment rescale choice, cleaner input,
      cleaner output, bypass alignment, final normalization and output metadata
      from caller-supplied discrete parameters and the native profile.
- [x] The implementation reuses actual cleaner depth; callers and tests no
      longer maintain a second affine or depth formula as their result oracle.
- [x] Native bypass capacity remains distinct from final discrete output.
      Existing BootParameter public behavior and core-parameter defaults remain
      compatible; the route output interface reports the discrete result.
- [x] Both BootContext construction overloads reuse common core-parameter
      checks while enforcing their explicit route-specific parameter contracts.
- [x] Invalid combinations fail during creation or preparation, before
      evaluation: include unsupported alignment levels or rescale schedules,
      non-finite scale inputs, insufficient bypass capacity and incompatible
      route/manifest combinations.
- [x] Selected fixed expectations independently assert Identity19 L21, Discrete
      MEBOOT L20 and Ordinary L20, all at scale 2^40.
- [x] Supported research schedules use explicit expected levels and output
      scales in parameterized cases; selected and previously supported
      configurations preserve encrypted behavior and existing error gates.
- [x] Selected fused bypass binding and supported rescaled bypass alignment
      remain correct; real and imaginary channels finish in application units.
- [x] Coefficient manifests remain explicit caller input. Preparation validation
      is distinguished from encrypted numerical certification.
- [x] Tests use existing creation, preparation and route execution interfaces;
      prepared records do not become a new public orchestration requirement.
- [x] Record affected build, parameter rejection and encrypted route results.
      Keep Ordinary and native Identity19 regression coverage.

**Completion evidence:** One owner computes the discrete Scaling Binding,
tests assert independent fixed contracts, and supported encrypted routes pass.
This ticket can land without entry or profiling consolidation. Coordinate edits
with ticket 01 if both are developed concurrently.

Verified on 2026-10-19: the final full GPU build passed. Production regression
passed 11/11 targets (45 cases); final research regression passed 3/3 targets
(5 cases). Additional alignment-28 and d95 encrypted configurations passed
with fixed L19 expectations and unchanged numerical gates. See the
[validation report](../../../docs/validation/bootstrap-contract-deepening-20261019.md)
for detailed/coarse stage checks, equivalence, commands and retained limitations.
