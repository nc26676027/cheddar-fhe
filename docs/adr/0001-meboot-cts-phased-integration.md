# Phased Integration of MEBOOT CtS into Cheddar

## Context
Cheddar's CKKS bootstrapping uses a butterfly-network CtS based on generic BSGS and hoisted rotations, consuming multiple modulus levels. MEBOOT introduces a four-role decomposition (M1 Subring Prefix, M2 LCR+AKS, M3/M4 Dense BSGS Folding) that reduces modulus consumption and rotation key footprint. Directly porting M1 requires introducing subring ciphertext representations and NTT layouts on GPU, which carries high complexity.

## Decision
We integrate MEBOOT CtS as a modular extension (`include/extension/MEBootCtS.h`) selectable within `BootContext`. We proceed in two phases: Phase 1 implements M2 (LCR + AKS) and M3/M4 (BSGS Folding) over the full ring $R_Q$ using Cheddar's existing GPU primitive operators before fusing kernels. Phase 2 evaluates adding M1 Subring support.

## Consequences
- Allows A/B verification against existing `EvalSpecialFFT` without breaking existing workflows.
- Eliminates one modulus drop in CtS during Phase 1 while avoiding premature subring complexity on GPU.
- Requires generating and storing AKS-encoded evaluation keys specifically for M2.
