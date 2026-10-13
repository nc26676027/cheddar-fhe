# MEBOOT precision and Basic latency targets

The owner confirmed these priorities on 2026-10-12 after the Phase 1 performance
review: preserve the verified one-level CtS saving, bring MEBOOT precision close
to the standard path (approximately 129 dB in the recorded 32-bit run), and
match the standard Basic path's end-to-end latency. Total key/device memory is
an additional measurement, not permission to trade away precision. Phase 2
ring dimension drop remains outside this task.

Compare the same input ciphertext and main/sparse secrets, with the same CKKS
parameter profile. Preserve the current depth policy: 32-bit MEBOOT starts one
level lower and retains the standard output level; 64-bit MEBOOT retains one
more output level. Report those differences beside precision and latency.

Record relative SNR and maximum complex error against standard Basic, as well
as absolute SNR. Use a 1 dB relative-SNR loss as an initial regression alarm
for paired runs; the final evidence must show precision close to the standard
baseline, rather than simply passing the former absolute 1e-3 error tolerance.
Inputs are deterministic test vectors; key/encryption randomness remains real.

Time repeated, alternating standard/MEBOOT executions after warmup, excluding
matrix/key preparation and encryption/decryption. Report median and spread,
host wall-clock and CUDA-event timing, and identify actual stage bottlenecks
before changing kernels. Single timing samples do not establish the target.
