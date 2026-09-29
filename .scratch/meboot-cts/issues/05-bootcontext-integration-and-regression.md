# 05: BootContext Integration and End-to-End Regression

**What to build:**
Integration of `MEBootCtS` into Cheddar's `BootContext`, selectable via bootstrapping configuration parameters. Full end-to-end bootstrapping (`BootContext::Boot`) execution passing all existing correctness checks for both 32-bit and 64-bit word types (`bootparam_40.json` and `bootparam_40_64bit.json`).

**Blocked by:** 04: Four-Stage MEBootCtS Module and Slot Accuracy

**Status:** ready-for-agent

- [ ] `BootContext` updated to instantiate and invoke `MEBootCtS` when configured
- [ ] End-to-end bootstrapping tests in `unittest/Bootstrapping.cpp` pass with `MEBootCtS` enabled for 32-bit (`bootparam_40.json`)
- [ ] End-to-end bootstrapping tests pass for 64-bit (`bootparam_40_64bit.json`) with zero regressions in decrypted message SNR and verified level reduction
