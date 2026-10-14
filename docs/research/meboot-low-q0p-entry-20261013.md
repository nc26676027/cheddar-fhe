# 64-bit low-q0P entry correctness experiment

Date: 2026-10-13. This is a separate experiment; the production profiles
remain unchanged. The owner requested correctness before security screening.
The deferred security screen has not run, and this report makes no security
claim. The 32-bit two-prime entry is outside this experiment.

## Parameters

The experiment profile is `parameters/bootparam_40_64bit_lowp_entry.json`.
Removing its `entry_primes` field yields the exact parsed production
64-bit profile. Both production JSON files are byte-identical to the
pre-experiment files; their SHA-256 values are recorded in
`.scratch/low-q0p-entry/baseline-profiles.sha256`.

| Parameter | Production 64-bit entry | Experiment |
| --- | --- | --- |
| Full ring degree | 65536 | 65536 |
| Q0 | 1125899908022273 | 1125899908022273 |
| Entry P | 2305843009211596801 | 786433 |
| log2(Q0P) | approximately 111 | 69.5849643367 |
| Decomposition | one RNS chunk | 4-bit unsigned digits, J=13 |
| Sparse weight / support stride | 256 / 16 | 256 / 16 |
| Entry error standard deviation | 819.2 | 819.2 |
| EvalMod K | 64 | 64 |
| Normal modulus chain / normal KS | existing | existing |
| Entry evaluation-key payload | 2 MiB | 26 MiB |

The reference revision is 6330f64b in the adjacent
`paper1-linear-bootstrap` checkout. The experiment adopts its single
entry P and base-16 gadget, while retaining Cheddar's H, sigma and K.
It does not adopt the reference's security or precision results.
The golden matrix fixture retains its original revision a4415572.

## Evaluation contract

`EntryGadget` owns a separate Parameter and NTTHandler for Q0/P, reusing
the NTT implementation with independently populated prime buffers and
twiddle tables. Entry primes do not enter the normal auxiliary pool.

An entry key carries its explicit Q0/P identity. Normal MultKey paths
reject it. BootContext dispatches only the DtS step to this evaluator.
The ordinary rotation, multiplication, conjugation, StD and AKS paths
retain their RNS decomposition.

For digit j, key generation encrypts P*16^j*s_dense under s_sparse in
Q0/P. The message is zero in the P limb. Each digit samples a fresh
full-ring error polynomial at sigma=819.2; its Q and P residues represent
the same error. Evaluation INTTs the canonical Q0 A polynomial, extracts
13 unsigned four-bit windows, NTTs each over Q0/P, and accumulates the
digit products. ModDown subtracts the centered P residue and divides by
P in Q0. The original B is added after ModDown. In-place switching is
also verified.

## Phase (a): isolated correctness

`low_q0p_entry_test`: 3/3 passed.

- Exact GPU digits match host extraction in both limbs and reconstruct
  every original coefficient. Inputs include zero, Q0-1, Q0/2,
  digit boundaries and a deterministic spread across Q0.
- Three ordinary random encryptions switched from the dense secret to
  the actual stride-16 sparse secret. SNR was 170.737--170.765 dB;
  maximum slot error was 1.577e-7--1.596e-7. The maximum additional
  coefficient error was 33--35. Input encryption error was measured
  separately, approximately 2.4e-9--2.7e-9 in slots.
- A synthetic noiseless key verifies the integer ModDown residual
  independently of encoding and Gaussian error. At H=256, the two
  centered P residues give a coefficient bound of (H+1)/2; the test
  allows the integer ceiling 129. The observed maximum was 18.

The first noisy test used an arbitrary 1e-7 maximum slot-error gate and
failed, despite approximately 169--171 dB SNR. Instrumentation confirmed
that the DtS contribution, rather than initial encoding, exceeded this
gate. The final isolated test explicitly allows maximum additional slot
error below 1e-6, requires SNR at least 140 dB, and requires maximum
coefficient error below 512. These are experimental numerical gates,
not certified probabilistic bounds. The initial and diagnostic failure
logs are preserved in the evidence directory. The full-bootstrap
acceptance gates were not relaxed.

## Phase (b): complete paired bootstrap

`Experiment/LowQ0PPerformance64.PairedMEBootPrecisionAndLatency/0` passed.
Both paths share the same low-q0P entry key, application/subring secrets
and input ciphertext. Each path has 30 alternating timing samples.

| Measurement | Standard | MEBOOT |
| --- | --- | --- |
| SNR (dB) | 124.896770161 | 125.074569917 |
| Maximum slot error | 1.25821823817e-6 | 1.23591334667e-6 |
| Output level | 4 | 6 |
| Median wall latency (ms) | 98.20865 | 96.1177 |
| Median CUDA event latency (ms) | 91.90895 | 88.71795 |

The retained gates are 114 dB absolute SNR for both paths, MEBOOT SNR
within 1 dB of standard, MEBOOT maximum error below 1e-3, and two
additional output levels. The normal key ceiling is level 22 and the
temporary prefix starts at level 23.

Timings were measured on RTX 5060 Ti under WSL with CUDA 12.8.
They compare standard against MEBOOT within the experiment; they do not
measure the speed change from the production entry gadget.

## Normal-path regression

All five selected regression targets passed, totaling 11 cases:

- Production 32-bit and 64-bit paired full bootstrap: 2 cases.
- Ordinary RNS ModUp/ModDown, relinearization and rescale at every normal
  level of the original 30/35/40 profiles: 3 cases.
- Production 32-bit and 64-bit key-modulus boundaries, including
  rejection of OverModRaise evaluation-key requests: 2 cases.
- Centered CRT boundaries for both word widths: 2 cases.
- Centered single-prime ModUp for both word widths: 2 cases.

Including the two experimental phases, the final selected validation has
15 passing cases across 7 target runs. This is selected regression coverage,
not a claim that every repository test was run. The build and
`git diff --check` also passed.

## Reproduction and evidence

Run these commands from the repository root under WSL, with GPU tests
serial:

```bash
cmake -S . -B build -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
cmake --build build -j 4
build/unittest/low_q0p_entry_test
CHEDDAR_BENCH_REPEATS=30 build/unittest/meboot_performance_test \
  '--gtest_filter=Experiment/*'
bash .scratch/low-q0p-entry/run-regression.sh
```

Evidence is under `.scratch/low-q0p-entry/`: `domain.json`,
`baseline-profiles.sha256`, `environment.txt`, `build.log`,
`phase-a.log/xml`, `phase-b.log/xml`, and `phase-b-summary.json`.
The final regression status is recorded in `regression.tsv`, with
individual target logs and XML files.
`validation-summary.json` aggregates the final 15 cases, and
`source.sha256` records the tested source and parameter files.

Security screening of the actual entry Q0/P domain remains deferred.
Correctness qualification alone does not replace the adoption evidence
in ADR-0006.
