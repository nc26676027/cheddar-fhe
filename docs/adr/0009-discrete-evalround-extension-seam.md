# ADR-0009: Discrete EvalRound extension seam

Accepted 2026-10-15.

## Decision

Discrete CKKS is a library extension route with an explicit
`DiscreteIdentityBootstrap` interface. It does not replace
`BootContext::Boot()` or change the native Identity19 production dispatch.

The route is a Discrete EvalRound: the integer-cleaning execution is shared
by the ordinary and MEBOOT CtS adapters, while StC, entry switching, low CtS,
and accurate bypass remain route-specific. The shared execution module owns:

- the real and imaginary channel cleaning;
- common integer recombination;
- bypass scale and level alignment;
- subtraction and final output normalization;
- output level and scale checks;
- optional stage timing.

`DiscreteIdentityParameters` validates the extractor width, degree, finite
coefficients, input radius, digit error, and scale contract before execution.
The encrypted certificate remains evidence attached to the selected
parameter manifest; it is not inferred from ciphertext metadata.

## Rationale

Ordinary and MEBOOT are two EvalRound CtS choices. Keeping that variation at
the adapter seam lets future CtS implementations reuse the same integer
contract without creating another bootstrap orchestration line. The native
BootContext seam remains unchanged because the discrete route has a different
output level and precision contract from Identity19.

## Consequences

The public discrete route is robustly selectable and testable through one
shared execution interface. It still requires the caller to prepare the
route-specific rotation and AKS keys. Matched performance comparison and
cleaner basis batching remain separate follow-up work.
