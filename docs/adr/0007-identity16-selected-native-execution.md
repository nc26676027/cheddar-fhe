# Bind selected Identity16 execution to native Cheddar operators

Accepted scope on 2026-10-13: the owner selected StC-first EvalRound and both
Identity16-18 joint4434 and ordinary routes, with 64-bit execution first.
Use exact route-specific reference profiles and scaling bindings, while
executing through Cheddar's native NTT, transforms, hoisting, polynomial
compiler and fused arithmetic. This chooses full algorithm alignment over
changing only the classic bootstrap parameter values.

The selected joint profile uses an extension-8 Subring Prefix and an
independent qExtra removed before any main-Q/P key operation; ordinary retains
its own full-ring ephemeral entry. These are separate explicit contracts
alongside ADR-0004/0006's retained Phase 1 profiles, not changes to their
stride-16/H=256/error-sigma assumptions. Security screening remains deferred.

LCR range eligibility, selected scaling/rounding, peak memory and matched-work
latency require evidence before qualification. Existing kernel fusion remains
the execution default. Numerical acceptance gates and ticket granularity remain
under review in the accompanying specification.

## Joint scaling update, 2026-10-14

The owner subsequently authorized retuning the joint Scaling Binding while
explicitly preserving q0. The complete Q/P chains, qExtra, entry P, secrets,
18 application levels, level20/scale2^40 output and 2^-18 gate remain fixed.
Ordinary keeps its imported reference binding.

The joint candidate uses StC scales 2^40 -> 2^37.875 -> 2^33 with the original
1/64 physical matrix gain. ScaleDown retains integer multiplier one; its old
round-to-zero target is no longer used. The centered lift multiplier is 32768.
Proper CtS gain doubles, restoring the same lifted integer period as the
reference's multiplier65536. Mod1 and correction return gains halve, while
the independent bypass retains gain0.25. Scaling the initial CosDiscrete
coefficients and sqrt2pi by 2^(-1/8) propagates to gain1/2 after three
double-angle steps without changing degree or consuming another level.

The earlier scale2^34 candidate passes the original test patterns and is
retained as diagnostic evidence. A preliminary objection compared decoded StC
slots with the q0 coefficient boundary; that objection was incorrect. Encoder
IFFT inverts StC before coefficient encoding, so those representations must
not be mixed. The scale2^33 candidate is additionally checked on the valid
all-slot input 0.25+0.25i. The actual encoded LCR norms are checked using the
candidate's lift multiplier.

The pinned reference export remains immutable provenance. The native importer
records the scaling delta explicitly. An independent local Go overlay applies
the same new binding for reference comparisons without editing the reference
checkout. This is a tuned reference variant, not the original frozen evaluator.

## Joint HW40 range correction, 2026-10-15

The owner selected K20, actual degree62 and two double-angle steps for the
extension-8/HW40 joint route. The retained stride-16/HW256 tail calculation
does not apply to this entry domain. Domain escape, lattice attack cost and
numerical approximation are separate checks; see
`docs/research/identity19-verification-20261015.md`.

Q/P, qExtra, entry P and the StC scaling remain fixed.
The joint CtS still executes L32 -> L31 -> L31 -> L30 -> L29. Degree62
consumes six levels to L23, then DA2 returns L23 -> L22 -> L21.
The bypass consumes eight levels from L29 to L21, and the correction
consumes L22 to L21. The initially added L29-to-L28 DropLevel was an
unnecessary loss and has been removed. Output level is derived from the
actual Mod1 depth per route: Joint L21, Ordinary L20.

The half-gain Mod1 binding requires initial coefficients and sqrt2pi to be
multiplied by 2^(-1/4) for DA2. The earlier 2^(-1/8) binding remains the
DA3 formula only. The importer derives this exponent from the DA count and
checks a separate pinned K20 polynomial export; the original reference export
remains immutable. Ordinary retains K16/degree30/DA3 and its original arrays.
