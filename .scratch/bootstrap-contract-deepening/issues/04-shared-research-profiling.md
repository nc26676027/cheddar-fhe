# 04: Migrate detailed discrete research profiling to shared timing

**What to build:** The discrete baseline and MEBOOT discrete research profiling
interfaces reuse the timing implementation adopted by production profiling.
Detailed channel, digit and cleaning-iteration observations remain available.
The observation adapter preserves its own stage meaning while eliminating
duplicate CUDA event mechanics.

**Blocked by:** 03: Adopt shared timing through production ProfileBoot.

**Status:** completed

**Specification:** Bootstrap Contract Deepening, research timing workstream.

- [x] Both discrete research Profile callers use the timing implementation from
      ticket 03 through an observation adapter.
- [x] Detailed observer labels and order, existing result fields and their host
      duration meanings remain compatible with research consumers.
- [x] Real and imaginary cleaner observations, digit and iteration events, and
      ReleaseTemps remain present for the currently supported selected route
      and baseline.
- [x] Detailed research intervals and coarse production intervals remain
      explicitly distinct; the implementation does not rename or regroup them
      into an artificial common taxonomy.
- [x] The old research event arrays, synchronization and elapsed-time machinery
      are removed after all research callers migrate.
- [x] The shared module handles the detailed event count for supported research
      schedules without storage overflow and retains the checked exhaustion
      behavior established in ticket 03.
- [x] Research profiling and normal evaluation preserve equivalent decrypted
      output, level and scale for the discrete baseline and selected discrete
      MEBOOT route under their existing numerical gates.
- [x] Profiling success and evaluation failure reuse the timing module's
      cleanup behavior; repeated profiling does not retain event resources.
- [x] Production ProfileBoot stage semantics and output equivalence checks from
      ticket 03 still pass after the research migration.
- [x] Record affected GPU build, detailed stage checks and encrypted baseline /
      MEBOOT profiling results. Avoid fixed-latency assertions or a new
      profiling abstraction.

**Completion evidence:** Both research callers use the shared timing module,
the duplicate event implementation is removed, and detailed research plus
production profiling regressions pass. Ticket 03 is the only prerequisite;
entry and Scaling Binding changes do not gate this migration.

Verified on 2026-10-19: the final full GPU build passed. Production regression
passed 11/11 targets (45 cases); final research regression passed 3/3 targets
(5 cases). Additional alignment-28 and d95 encrypted configurations passed
with fixed L19 expectations and unchanged numerical gates. See the
[validation report](../../../docs/validation/bootstrap-contract-deepening-20261019.md)
for detailed/coarse stage checks, equivalence, commands and retained limitations.
