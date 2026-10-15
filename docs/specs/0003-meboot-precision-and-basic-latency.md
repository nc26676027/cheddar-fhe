> Historical specification (2026-10-15): this records earlier design scope. ADR-0008 defines the current production Identity19 and Ordinary routes; previous classic and Phase 1 paths are superseded.

# MEBOOT precision and Basic latency targets

The owner confirmed these priorities on 2026-10-12 after the Phase 1 performance
review: preserve the CtS depth benefit, bring MEBOOT precision close
to the standard path (approximately 129 dB in the historical 32-bit run), and
match the standard Basic path's end-to-end latency. Total key/device memory is
an additional measurement, not permission to trade away precision. Phase 2
ring dimension drop remains outside this task.

Compare the same input ciphertext and main/sparse secrets, with the same CKKS
parameter profile. The 2026-10-13 correction separates the key-safe normal Q
chain from the key-free OverModRaise prefix. Standard CtS consumes four normal
levels; MEBOOT consumes two, plus one temporary prefix level. Both word types
retain two additional output levels at the same normal Q/P ceiling. Report
this depth policy beside precision and latency; do not classify ordinary
Cheddar terminal primes as temporary merely because of their storage name.

Record relative SNR and maximum complex error against standard Basic, as well
as absolute SNR. Use a 1 dB relative-SNR loss as an initial regression alarm
for paired runs; the final evidence must show precision close to the standard
baseline, rather than simply passing the former absolute 1e-3 error tolerance.
The absolute acceptance floors are 128 dB for 32-bit and 114 dB for 64-bit,
for both paths, in addition to the paired 1 dB alarm. A simultaneous loss in
both paths does not satisfy this requirement.
Inputs are deterministic test vectors; key/encryption randomness remains real.

The expanded h=256 / K=64 preset spends eleven EvalMod levels. Report the
resulting application output levels (standard/MEBOOT: 2/4 for 32-bit and 4/6
for 64-bit) alongside the CtS depth saving and actual Q/P security screen.

Time repeated, alternating standard/MEBOOT executions after warmup, excluding
matrix/key preparation and encryption/decryption. Report median and spread,
host wall-clock and CUDA-event timing, and identify actual stage bottlenecks
before changing kernels. Single timing samples do not establish the target.
