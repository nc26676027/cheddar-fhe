# 01: Share MEBOOT entry and centered lift across route adapters

**What to build:** Identity19 and Discrete MEBOOT evaluate through the existing
BootContext seam while sharing one deep entry-to-fork module. The module owns
StC, ScaleDown, Low-q0P entry switching, centered lift, physical gain and scale
binding, and preparation of the proper and dense branch inputs. Both branches
retain the same lifted integer. This provides locality for the entry contract
and leverage for two existing route adapters.

**Blocked by:** None (can start immediately).

**Status:** completed

**Specification:** Bootstrap Contract Deepening, shared MEBOOT entry workstream.

- [x] Identity19 and Discrete MEBOOT both use the shared implementation through
      BootContext; duplicated entry-to-fork coordination is removed from their
      route adapters.
- [x] The shared module owns the two ciphertext-part centered lifts, encoded
      lift gain and scale binding rather than forwarding to another shallow
      module that leaves callers coordinating these facts.
- [x] The proper and dense branch inputs originate from the same lifted
      ciphertext; existing encrypted observations verify common-integer
      behavior and supported real and imaginary inputs.
- [x] Normal-basis restriction precedes sparse-to-dense key switching;
      temporary prefix primes remain outside normal evaluation-key operations.
      Existing entry-key and modulus-ceiling rejection behavior is preserved.
- [x] Route-specific CtS gain, LCR qualification and nonlinear correction stay
      with their existing owners; encoded LCR checks continue to execute.
- [x] BootContext creation, preparation, rotation requests, AKS preparation,
      Boot and ProfileBoot retain their current caller workflow.
- [x] Identity19 returns L21 and selected Discrete MEBOOT returns L20 at scale
      2^40, satisfying their existing encrypted numerical gates and supported
      aliasing behavior.
- [x] Ordinary retains its full-ring entry and L20/scale-2^40 result; its
      encrypted route regression passes.
- [x] Existing diagnostic observation labels and meaningful stage positions
      remain compatible, including the production profiling stages.
- [x] Tests use BootContext and existing research observations, with independent
      encrypted oracles. No new public entry-stage interface is introduced.
- [x] Record the affected GPU build, route regressions, configuration and
      before/after numerical results. Explicitly retain pending validation if
      the execution environment cannot run the required GPU checks.

**Completion evidence:** Shared coordination removed from both MEBOOT adapters,
passing affected route and entry-contract checks, and recorded validation
results. This ticket lands independently of Scaling Binding and timing work.

Verified on 2026-10-19: the full GPU build passed; the production regression
selection passed 11/11 targets and 45/45 cases. See the
[validation report](../../../docs/validation/bootstrap-contract-deepening-20261019.md)
for commands, output contracts, timings and retained baseline limitations.
