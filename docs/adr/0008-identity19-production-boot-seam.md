# ADR-0008: Identity19 production boot seam

Accepted 2026-10-15. Supersedes the production-route decisions in ADR-0001
through ADR-0007. Those records remain dated design and validation history.

## Decision

The only production MEBOOT implementation is the selected, native Cheddar
StC-first EvalRound path. The default route is Identity19 Joint: ring log degree
16, full 32768 slots, 64-bit words, input L2/scale 2^40, HW40 extension-8
Subring Prefix, 42-bit q0 with Low-q0P entry, temporary qExtra, K20 EvalMod
with actual polynomial degree 62 and DA2, output L21/scale 2^40. This is 19
levels above the input. The supported Ordinary route keeps its own full-ring
entry, K16, actual degree 30, DA3 and output L20/scale 2^40.

`BootParameter::Selected(BootRoute::kMEBoot)` selects the default route;
`BootRoute::kOrdinary` selects Ordinary. The production sequence is
`MakeParameter()`, `BootContext<uint64_t>::Create(*param, boot_param)`,
`Prepare()`, `AddRequiredRotations(req, kSlots)`, client
`PrepareRotationKey(req)`, MEBOOT-only
`PrepareMEBootAKSKeys(context->GetMEBootCtS(kSlots))`, then
`Boot(output, input, evkmap)`. `ProfileBoot` measures that same path.
Key preparation stays with `UserInterface`; evaluation uses Cheddar's native
key maps and ciphertext operators. The profile owns the route-specific chain,
scaling binding, entry contract, polynomial and output level.

`BootContext` is the external seam. Its implementation delegates the selected
pipeline to `MEBootExecution`, `BootTransforms`, `MEBootEvalMod` and the
existing `MEBootCtS` and Cheddar core operators. Stage planning, LCR, AKS,
linear transforms, NTT, hoisting, rescale and fused kernels remain reusable
implementation details. No classic bootstrap dispatch or `EvalSpecialFFT`
implementation remains in the production route. Do not add a second wrapper
around `BootContext` or expose stage methods merely for production callers.

## Evidence and limits

The 2026-10-15 numerical and timing results from the consolidated public
`BootContext` seam are recorded in `docs/evidence/identity19/`. The earlier
selected implementation remains available as labeled baseline evidence in
`docs/evidence/identity19-baseline/`. The current run used one prepared keyset;
three-keyset qualification remains a separate acceptance target.
The independent selected stage oracle belongs at
`unittest/fixtures/identity19-stage-oracle.json.gz`; the pinned reference
export remains under `parameters/reference/`.

The classical security screen is a configuration-matched heuristic, not a
proof of IND-CPA security. `docs/evidence/identity19/security.json` binds the
current profile hash and exact key domains to the pinned reference estimates.
The upstream configuration retains its original `identity16-18-joint` name.
Domain escape, numerical approximation and lattice attack cost remain separate
claims.
