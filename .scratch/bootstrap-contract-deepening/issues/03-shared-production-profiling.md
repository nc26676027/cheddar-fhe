# 03: Adopt shared timing through production ProfileBoot

**What to build:** Production callers continue using BootContext::ProfileBoot
and receive its existing stage and result semantics, with CUDA event ownership,
recording, synchronization, duration calculation and cleanup concentrated in
one deep internal timing module. This provides locality for profiling mechanics
and an implementation that the existing research observation adapter can reuse.

**Blocked by:** None (can start immediately).

**Status:** completed

**Specification:** Bootstrap Contract Deepening, production timing workstream.

- [x] ProfileBoot uses the internal timing implementation for event allocation,
      recording, synchronization, duration conversion and cleanup.
- [x] Existing public result fields, coarse route stage labels and order, host
      submission duration meaning and the final ReleaseTemps interval remain
      compatible.
- [x] ProfileBoot executes the same route as Boot; selected Identity19,
      Discrete MEBOOT and Ordinary results preserve their expected metadata and
      existing decrypted numerical acceptance gates.
- [x] Both supported profiling and ordinary evaluation handle input/output
      aliasing consistently with the existing route contract.
- [x] Timing values are finite and nonnegative; verify stage order and duration
      consistency without exact-latency assertions.
- [x] Event capacity handling is explicit and checked before writing beyond
      storage. The supported stage count completes successfully; exhaustion
      either grows safely or fails with the established error convention.
- [x] Success, an evaluation callable that throws and capacity handling release
      the timing module's acquired resources. Exercise these through its
      interface only when the bootstrap seam cannot trigger them reliably.
- [x] Host timing and GPU timing remain separate measures; the implementation
      checks required CUDA operation results.
- [x] The module accommodates the already-existing stage-marker and detailed
      observation adapters without exposing a generic timing backend or a new
      production stage interface.
- [x] The research profiler remains usable during this additive migration;
      migration of its callers belongs to ticket 04.
- [x] Record the affected GPU build, profiling/output equivalence checks and a
      fixed-configuration descriptive before/after measurement. A timing
      refactor is not reported as a speedup without supporting evidence.

**Completion evidence:** Production ProfileBoot uses the shared event lifecycle
and passes route/output, stage semantics and cleanup validation. This ticket
lands independently while leaving research profiling functional.

Verified on 2026-10-19: the full GPU build passed; the production regression
selection passed 11/11 targets and 45/45 cases. See the
[validation report](../../../docs/validation/bootstrap-contract-deepening-20261019.md)
for commands, output contracts, timings and retained baseline limitations.
