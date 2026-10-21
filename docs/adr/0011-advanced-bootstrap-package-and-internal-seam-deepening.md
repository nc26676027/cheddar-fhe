# ADR-0011: Advanced Bootstrap package and internal seam deepening

Accepted 2026-10-21.

## Context

Following the completion of the logN16 Discrete MEBOOT latency optimization campaign (Spec 0006), the GPU latency of `BootRoute::kDiscreteCKKSMeBoot` was reduced from 668.0 ms to 561.5 ms (-16.0%) while maintaining zero-slack invariants (L20 output, scale 2^40, worst bits > 18.0, clean integer error < 2^-33, and peak VRAM safely within the 15,385 MB WDDM ceiling).

However, the architecture retained friction from its experimental phase:
1. `BootContext` was a shallow routing facade requiring external callers to manually construct and inject `DiscreteIdentityParameters` (including 384 Remez polynomial coefficients from disk) to initialize the discrete route.
2. Internal execution classes (`MEBOOTDiscreteIdentity`, `DiscreteIdentityExecution`) were exposed in public headers (`include/extension/DiscreteIdentityBootstrap.h`), leaking internal algorithms, Paterson-Stockmeyer tree shapes, and memory chunk management across the public seam.
3. Temporary experiment tickets, benchmark logs, and skipped test files remained scattered across `.scratch/` and `unittest/`.

## Decision

1. **Self-Contained Advanced Bootstrap Seam**:
   `BootContext::Create(param, BootParameter::DiscreteCKKSMeBoot())` and `BootContext::Create(param, BootRoute::kDiscreteCKKSMeBoot)` now provide zero-argument one-line factory initialization for Discrete MEBOOT. The certified degree-63 Remez polynomial coefficients, baby-step 16, alignment scale, and cleaner parameters are fully compiled into Cheddar as authoritative defaults. External callers no longer need to read manifest files from disk or know about `DiscreteIdentityParameters`.

2. **Internal Class Encapsulation**:
   `MEBOOTDiscreteIdentity` and `DiscreteIdentityExecution` are sunk into private internal headers (`src/extension/DiscreteIdentityInternal.h`). The public header `include/extension/DiscreteIdentityBootstrap.h` retains only the parameter descriptor and data structures required for optional custom tuning. Callers and test suites interact exclusively through `BootContext` and `BootstrapExecution`.

3. **Retirement of Experimental logN16 Scaffolding**:
   The active Spec 0006 sprint is marked as completed and concluded. The temporary directory `.scratch/discrete-meboot-latency/`, redundant test-skip benchmarks (`DiscreteCleaningPerformanceTest.cpp`), and obsolete single-run failure logs are deleted.

## Rationale

- **High Leverage & Zero Caller Burden**: Callers only need to know their desired output contract (Identity19 for 19 restored levels at L21; DiscreteCKKSMeBoot for certified exact integer extraction at L20). All polynomial approximation constants, NTT cache sizing, and key switch schedules stay behind the seam.
- **High Locality**: Future maintenance, kernel fusion, and memory optimizations can be performed entirely within the package without breaking downstream users.
- **The Interface is the Test Surface**: Tests exercise the production interface `BootContext::Create` / `Boot` / `ProfileBoot`, preventing drift between research testing and production behavior.

## Consequences

- Callers can initialize both `Identity19` and `DiscreteCKKSMeBoot` with identical single-line calls without external filesystem dependencies.
- The optional 3-argument overload `BootContext::Create(param, boot_param, custom_params)` remains supported for specialized research configurations.
- All 6 core test suites pass without regression.
