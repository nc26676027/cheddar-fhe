# Spec: Codebase Architecture Deepening and Module Consolidation

## Problem Statement

Developers and maintainers extending Cheddar FHE face architectural friction caused by shallow pass-through modules, high coupling across internal seams, and leaky module abstractions. 

Specifically, linear transformations are split across an empty pass-through wrapper and an execution handler, forcing callers to manually convert structured matrices into fragile nested hash maps of messages. Evaluation key storage relies on public inheritance from raw unordered maps with magic numeric indices, allowing arbitrary mutation and leaking private cryptographic secrets directly across the client-server seam through testing facades. Furthermore, modulus reduction operations leak internal twiddle factor layouts and auxiliary prime memory offsets across the arithmetic transform seam, creating fragile coordination points that trigger silent numerical errors whenever parameter topologies change. Finally, the newly introduced MEBOOT multi-stage pipeline currently requires callers to orchestrate individual modulus lifts, key dispatches, and level-conserving projections manually.

These architectural weaknesses reduce locality (bugs and coordination logic scatter across multiple files), dilute leverage (callers must learn wide, low-level interfaces to execute simple operations), and make automated testing brittle. Maintainers need these shallow modules consolidated into deep modules with clean, minimal seams once the core MEBOOT CtS migration is complete.

## Solution

Deepen the core and extension architectures of Cheddar FHE by consolidating shallow modules, placing clean seams, and eliminating data leakage across interfaces:

1. **Unified Linear Transformation Module**: Collapse the shallow linear transform wrapper and the hoisting engine into a single deep linear transformation evaluator that accepts structured diagonal matrices directly, handles BSGS partitioning and hoist encoding internally, and executes homomorphic matrix multiplication behind an atomic evaluation seam.
2. **Encapsulated Evaluation Key Store**: Replace raw map inheritance and magic numeric indices with an opaque, typed evaluation key store. The key store encapsulates storage, metadata validation, and retrieval for rotation keys, AKS keys, relinearization keys, and automorphism keys, while preventing secret material from leaking into public interfaces.
3. **Autonomous ModDown Operator**: Encapsulate RNS modulus switching, twiddle factor alignment, and base conversion matrix multiplication behind an autonomous ModDown operator, ensuring prime partition indexing and twiddle memory layouts never cross the arithmetic seam.
4. **Autonomous Multi-Stage Pipeline Evaluator**: Consolidate MEBOOT stage planning, diagonal key dispatching, and level-conserving projection into a self-contained four-stage evaluator presenting a unified evaluation seam to bootstrapping.

This refactoring will be executed strictly after the functional integration and verification of MEBOOT CtS is complete, ensuring zero functional regressions.

## User Stories

1. As an FHE circuit developer, I want to execute a linear transformation by passing a structured diagonal matrix and an input ciphertext to a single evaluator, so that I do not have to manage intermediate nested dictionary representations.
2. As a bootstrapping engineer, I want the linear transformation module to handle BSGS folding and baby-step/giant-step partitioning internally, so that callers do not leak matrix stride and decomposition invariants.
3. As a library maintainer, I want the deletion of the shallow linear transform wrapper to concentrate complexity rather than scatter it, so that future matrix evaluation improvements only touch a single module.
4. As an FHE application architect, I want evaluation keys to be stored in an opaque container rather than a public hash map, so that callers cannot inadvertently mutate or corrupt the evaluation key state.
5. As a security engineer, I want private secret polynomials to remain private within the key generator, so that secrets never leak to evaluators or callers through public view accessors.
6. As a client generating keys, I want to store rotation keys and AKS diagonal keys in a unified key store using typed accessors, so that magic numeric indices are eliminated from the codebase.
7. As a server executing homomorphic operations, I want to query evaluation keys by their mathematical role (rotation distance, matrix diagonal index, relinearization level), so that key lookup is verified and type-safe.
8. As an arithmetic kernel developer, I want modulus reduction (ModDown) to be completely encapsulated behind an atomic module, so that twiddle factor layout changes do not break modulus switching logic.
9. As a library user experimenting with different parameter sets (with or without terminal primes), I want prime offset calculations to remain strictly internal to the modulus switching module, so that topology changes cannot cause silent prime-limb misalignments.
10. As a bootstrapping pipeline engineer, I want MEBOOT multi-stage linear transformations to evaluate through a single call, so that callers do not have to manually coordinate ModUp, AKS key queries, automorphism permutations, and ModDown projections.
11. As a test engineer, I want to test linear transformations and bootstrapping operations solely through their public interfaces, so that tests remain stable across internal CUDA kernel optimizations.
12. As a contributor reading the codebase, I want deep modules with small interfaces, so that understanding a feature does not require hopping across four shallow wrappers.
13. As an autonomous AI coding agent, I want clean, non-leaking module seams, so that localized refactorings can be implemented and verified without triggering spooky action at a distance.

## Implementation Decisions

- **Linear Transformation Deepening**: The linear transformation wrapper and hoisting handler are unified into a single evaluator module. The module's public interface accepts the high-level context, the structured diagonal matrix, and evaluation keys, hiding all stride deduction, BSGS decomposition, and plain-hoist pre-encoding behind the evaluation seam.
- **Evaluation Key Store Encapsulation**: A dedicated, opaque key store replaces public inheritance from generic hash maps. The key store provides strictly typed registration and retrieval interfaces (e.g., querying by rotation step, Galois element, or AKS diagonal index). The key store owns the lifecycle and device memory allocation of stored evaluation keys.
- **Client/Server Seam Enforcement**: Private secret keys are strictly owned by a dedicated key generator module. Public accessors exposing raw secret device memory are eliminated; evaluation keys are generated and deposited directly into the evaluation key store.
- **Autonomous RNS ModDown Seam**: The modulus reduction workflow (INTT over auxiliary primes, matrix base conversion, and NTT epilogue) is co-located into a unified ModDown operator. Twiddle factor pointer calculations, auxiliary prime offsets, and temporary scratch buffers are managed entirely within the module implementation.
- **Unified Multi-Stage MEBOOT Evaluator**: The four-stage $[4, 4, 3, 4]$ MEBOOT linear transformation pipeline is encapsulated inside the CtS evaluator. Callers provide the input ciphertext and key store; the evaluator manages stage transitions, level conservation at level 4, and intermediate buffer re-use internally.

## Testing Decisions

- **Test Surface Discipline**: Tests must strictly cross the public module seams. No unit test may inspect private internal memory layouts, raw twiddle arrays, or intermediate scratch buffers.
- **Linear Transformation Testing**: Verified by applying known diagonal linear transformations to encrypted vectors and checking decrypted slot outputs against standard matrix-vector products across varying slot counts and BSGS parameters.
- **Key Store Testing**: Verified by registering evaluation keys through client key generation and retrieving them through server evaluators, verifying that missing keys trigger clean errors and valid keys perform correct homomorphic automorphisms.
- **ModDown Precision & Boundary Testing**: Verified by performing modulus switching across diverse parameter profiles (including profiles with non-zero terminal prime counts) and verifying that decrypted plaintext noise remains strictly within analytical CKKS bounds.
- **End-to-End Bootstrapping Seam**: Verified through the full bootstrapping test bed, ensuring that message signal-to-noise ratio (SNR) and level conservation are preserved without regressions.

## Out of Scope

- Rewriting CUDA NTT kernels or changing the underlying Cooley-Tukey / Stockham algorithms.
- Modifying the mathematical formulation of CKKS or MEBOOT algorithms.
- Subring prefix implementation (Phase 2 of MEBOOT remains separate).
- Serialization format modifications for persistent disk storage of ciphertexts.

## Further Notes

- **Execution Gate**: Implementation of this specification must commence only after all 5 tickets of the initial MEBOOT CtS port (`.scratch/meboot-cts/issues/01` through `05`) are completed, fully tested, and merged into the main codebase.
- **References**:
  - Architecture review report: `architecture-review-20260930.html`
  - MEBOOT CtS specification: `docs/specs/0001-meboot-cts-port.md`
  - Architecture decision records: `docs/adr/0001-meboot-cts-phased-integration.md`, `docs/adr/0002-aks-and-lcr-gpu-design.md`, `docs/adr/0003-stage-decomposition-and-matrix-verification.md`
  - Domain glossary: `CONTEXT.md`
