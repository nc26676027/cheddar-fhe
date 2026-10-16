# ADR-0010: Discrete MEBOOT as a BootContext route

Accepted 2026-10-16.

## Decision

Discrete CKKS MEBOOT is a selectable `BootRoute::kDiscreteMEBoot`. The public
bootstrap seam remains `BootContext::Create`, `Prepare`, `AddRequiredRotations`,
`Boot` and `ProfileBoot`. Callers select the route through `BootParameter` and
provide the certified `DiscreteIdentityParameters` manifest to the discrete
overload of `BootContext::Create`.

`BootstrapExecution` is the internal interface at that seam. Native MEBOOT
uses `MEBootExecution`; discrete MEBOOT uses `MEBOOTDiscreteIdentity`. Both
adapters reuse Cheddar's `BootTransforms`, `MEBootCtS`, `LinearTransform`,
entry gadget, native key map, NTT, centered ModUp, rescale and multiplication
operators. Route-specific integer extraction and cleaning remain inside the
discrete adapter.

The route owns its output contract. The selected discrete profile returns the
certified L20/scale-2^40 result for the current degree-63 Remez extractor and
two cleaning iterations. Native MEBOOT continues to return Identity19 L21 at
scale 2^40; Ordinary continues to use its existing route and profile.

## Rationale

The previous `DiscreteIdentityBootstrap` entry was a useful research adapter
but required callers to reproduce bootstrap orchestration and key preparation.
That made the interface shallow and left the production seam unaware of the
route. The new interface concentrates dispatch, preparation and profiling in
`BootContext`, while keeping the route implementations replaceable behind one
small seam.

The coefficients and numerical certificate remain caller-supplied
`DiscreteIdentityParameters` rather than hidden file-system state. This keeps
the compiled library independent of research manifest paths while allowing
the upper bootstrap option to bind a certified extractor explicitly.

## Consequences

Native routes retain their existing defaults and key contracts. Discrete
callers must supply the certified extractor parameters and prepare the same
rotation and MEBOOT AKS keys through `BootContext::GetMEBootCtS`. Detailed
stage observers remain available on the research adapter; `ProfileBoot`
provides the common route-level timing seam.

ADR-0009's statement that discrete execution must not alter
`BootContext::Boot` dispatch is superseded by this decision. Its shared
cleaning and common execution design remains valid.
