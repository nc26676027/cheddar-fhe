# 08: Run exact selected refreshes within the native GPU memory budget

**What to build:** Both complete selected routes can prepare and execute
serially on the target GPU with an explicit peak-live memory budget while
preserving their exact arithmetic, output contracts and steady-state dataflow.

**Blocked by:** 06: Complete the selected ordinary StC-first identity refresh;
07: Complete the selected joint StC-first EvalRound identity refresh.

**Status:** execution-in-progress (owner authorized 2026-10-13)

- [ ] Measure actual Cheddar key/diagonal/runtime payloads and preparation
  peaks separately; reference CPU payloads are not claimed as GPU peaks.
- [ ] Reuse compact native plaintexts, level-specific evaluation keys,
  preparation-only release and per-evaluator RMM scratch ownership.
- [ ] Keep proper/bypass/correction lifetimes explicit and avoid redundant
  resident arrays; shared keys and packed coefficient views retain identity.
- [ ] Fit within currently available GPU memory, including allocator/workspace
  overhead, without reducing Q/P, output depth, accuracy or key domains.
- [ ] No steady-state intermediate host transfer, stage synchronization or
  per-call re-encoding/key generation is introduced to save memory.
- [ ] Full outputs retain the pre-optimization numerical contract; exact
  residue differential tests cover ownership-only changes.
- [ ] Publish measured preparation time, retained bytes, peak-live bytes and
  hot-path allocation counts for both routes.

## Execution evidence (2026-10-13)

In progress. Sixteen-giant-step lossless plaintext compression permits ordinary execution. Ordinary measured RMM peak-live 14069516464 bytes, prepared live 13135235248 bytes; complete paired route peak and final correctness qualification remain.

Acceptance checkboxes remain open until their complete evidence exists. See ../execution/status.md for cross-ticket qualification.

## Continuation evidence

Both routes pass three packed fresh-key diagnostics within RMM peak-live 14069516464 bytes ordinary and 14492416976 bytes joint. Device-wide one-second sampling peaks at 15477 MiB; samples may miss short transients. Exact-public residue downloads are diagnostic-only and increase observed allocation peaks; full preparation accounting remains open.
