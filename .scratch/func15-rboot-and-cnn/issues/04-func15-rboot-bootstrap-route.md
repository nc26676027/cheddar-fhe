# 04: Func15 RBOOT Bootstrap Route

**What to build:**
Integrate the complete `Func15` RBOOT functional bootstrapping pipeline into `BootContext::Boot` under route `BootRoute::kFunc15RBoot`. Connect dense-to-subring 4-bit entry gadget, subring prefix, 4-stage low CtS ($[3, 3, 4, 4]$), functional EvalMod (ReLU activation), and accurate bypass alignment. Demonstrate end-to-end full-slot functional bootstrap on GPU from Level 2, Scale $2^{43}$ to Level 3, Scale $2^{43}$.

**Blocked by:** 02: LogN15 Parameter and Matrix Planning, 03: GPU RBOOT Functional EvalMod

**Status:** completed

- [x] `BootContext::Create(param, BootRoute::kFunc15RBoot)` initializes the complete functional route in one line.
- [x] 4-bit base-16 entry key switch correctly transitions dense secret ($N = 32,768$) to subring secret ($H = 80$, $N' = 4,096$).
- [x] Low CtS correctly transforms 16,384 coefficients into plaintext slots across stages $[3, 3, 4, 4]$.
- [x] Accurate alignment matches the functional output contract at Level 3, Scale $2^{43}$.
- [x] `func15_rboot_test` passes for all 16,384 slots on fresh ciphertexts: input Level 2, Scale $2^{43} \to$ output Level 3, Scale $2^{43}$, evaluating $\text{ReLU}(m)$ with mathematical error $0.0025 < 0.05$ and HE excess error $7.35 \times 10^{-6} \ll 0.05$.
- [x] Measured GPU median latency for single full-slot `Func15` bootstrap reported: **74.3 ms** for Activation, **66.0 ms** for Identity (vs. CPU ~9.29 s, a **125x - 140x speedup**, >220,000 activations/s).
- [x] Peak allocation stays strictly within the 15,385 MB WDDM ceiling (~3.2 GB).
