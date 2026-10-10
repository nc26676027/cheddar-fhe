# 03: Autonomous ModDown and ModUp Operator

**What to build:**
Deepen `ModSwitchHandler<word>` into an autonomous modulus switching operator that encapsulates terminal-prime offset calculations (`prime_offset`), active gadget digit counts (`beta`), ModUp scratch buffer allocation, and ciphertext-level `ModDown` / `Rescale` / `ModDownAndRescale` execution. Hide internal `Parameter` ModDown helpers from public interfaces.

**Blocked by:** 02

**Status:** completed

- [x] Encapsulate `prime_offset`, `beta`, and ModUp buffer allocation/execution inside `ModSwitchHandler<word>`
- [x] Provide autonomous ciphertext/polynomial ModDown and ModUp operations on `ModSwitchHandler<word>` used by `Context`, `LinearTransform`, and `LCROperator`
- [x] Restrict `Parameter::__GetPrimesPtrModDownWithTerPrimes` and `__GetInvPrimesPtrModDownWithTerPrimes` to private/friend access so internal prime layouts do not leak
- [x] Verify ModDown precision and terminal-prime topology handling across `bootparam_30.json`, `bootparam_35.json`, `bootparam_40.json`, and `bootparam_40_64bit.json`
