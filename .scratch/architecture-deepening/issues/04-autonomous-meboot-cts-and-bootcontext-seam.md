# 04: Autonomous Multi-Stage MEBOOT Evaluator and BootContext Seam

**What to build:**
Consolidate `MEBootCtS<word>` and `BootContext<word>` so that the 4-stage $[4, 4, 3, 4]$ CtS pipeline evaluates through a single unified `Evaluate(res, input, evk_map, min_ks)` call using the unified `EvkMap<word>`, and `BootContext::Boot(res, input, evk_map, min_ks)` / `BootContext::CoeffToSlot(res, num_slots, input, evk_map, min_ks)` work identically whether standard CtS or MEBOOT CtS is enabled.

**Blocked by:** 01, 02, 03

**Status:** completed

- [x] Add unified `MEBootCtS::Evaluate(res, input, evk_map, min_ks)` and `UserInterface::PrepareMEBootAKSKeys(const MEBootCtS<word> &)`
- [x] Unify `BootContext::Boot` and `BootContext::CoeffToSlot` so callers only need to pass `evk_map` (retaining backward-compatible overloads with explicit `aks_map`)
- [x] Verify all unit and end-to-end bootstrapping tests pass with zero regressions in SNR, level conservation, or memory lifecycle
