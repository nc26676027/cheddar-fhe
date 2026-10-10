# 02: Encapsulated Evaluation Key Store and Client/Server Seam Enforcement

**What to build:**
Replace public inheritance from `std::unordered_map<int, EvaluationKey<word>>` and public magic integer constants in `EvkMap<word>` and `AKSKeyMap<word>` with opaque, type-safe evaluation key stores. Embed `AKSKeyMap<word>` access inside `EvkMap<word>` so callers can pass a single unified key store. Eliminate public secret polynomial view accessors (`MainSecretConstView`, `SparseSecretConstView`) from `UserInterface<word>`.

**Blocked by:** 01

**Status:** completed

- [x] Remove public `std::unordered_map` inheritance and hide magic index constants in `EvkMap<word>` and `AKSKeyMap<word>`, providing typed registration and lookup interfaces
- [x] Integrate `AKSKeyMap<word>` into `EvkMap<word>` so client key generation deposits AKS keys into the unified key store
- [x] Make `MainSecretConstView` and `SparseSecretConstView` private in `UserInterface<word>` and provide `EncryptUnderContract` at the client seam
- [x] Update and expand `unittest/AKSKeyTest.cpp` to verify opaque key store registration, retrieval, and missing-key error handling across the client-server seam
