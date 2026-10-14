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
