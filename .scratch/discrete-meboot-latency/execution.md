# Discrete MEBOOT Latency: Execution

Started 2026-10-19 from `e107da8`. Specification (`docs/specs/0006-discrete-meboot-latency.md`),
ticket breakdown and test seams were approved before implementation.

## Plan and Progress

| Ticket | State | Verification |
| --- | --- | --- |
| 01 Split DigitExtract profiling & rename `accurate-bypass` | Completed | `bootstrap_timing_test` (3/3), `bootstrap_contract_test` (7/7), `discrete_integer_cleaner_test` (5/5), `discrete_identity_baseline_test` (1/1, 53 stages), `meboot_discrete_identity_test` (1/1, 62 stages), historical log replay via `summarize_discrete_depth_campaign.py` passed |
| 02 Interleaved ABAB rebaseline with same-session Identity19 | Completed | 3 keysets × 3 candidates (`pre_cubic`: 679.957 ms, `current`: 668.038 ms [-1.75%, non-overlapping 95% CI], `identity19`: 267.147 ms; gap: 400.891 ms) saved in `docs/evidence/discrete-identity/baseline-20261019/` |
| 03 Single-stream real/imag batching & parameter tuning | Completed | Evidence in `docs/evidence/discrete-identity/single-stream-batching-20261019/`: `current` GPU median 567.095 ms (-15.11% vs raw baseline), output L20, worst bits 18.844, clean_error 1.77e-11, VRAM peak 15,364 MB (< 15,385 MB) |
| 04 Digit representation CPU prototypes | Completed | `tools/research/prototype_digit_representations.py`, `docs/evidence/discrete-identity/prototypes-20261019/digit-representation-certificates.json`, and `docs/research/discrete-identity/digit-representation-prototypes-20261019.md` completed; both (a) 3×2-bit (9–12 levels) and (b) direct integer (14 levels / Runge blowup) rejected |
| 05 Operator Fusion & Deep Seams (Phases 2 & 3 + Deepening) | Completed | 0-deg leaf fusion, MultiCAccum recombination fusion (0.86 ms, -72%), Finish deep copy removal, MultiCAccumBatchDescriptor encapsulation, rvalue move semantics (zero deep copy of 30.4 MB ct in EvalPoly), and const_cast elimination; tests all green |

## Ticket 01 Observations (Single-Run Breakdown)

| Component | Real GPU (ms) | Imag GPU (ms) | Combined GPU (ms) | Share of DigitExtract |
| --- | ---: | ---: | ---: | ---: |
| `extract/basis` (shared power/Chebyshev basis, 13 ct-ct mults) | 54.866 | 55.655 | **110.521** | **28.6%** |
| `extract/tree/0..5` (6 output trees, ~6 ct-ct mults each) | 137.490 | 138.909 | **276.399** | **71.4%** |
| `extract/0..5/0` (`+0.5` offset `Add`) | 0.263 | 0.258 | 0.521 | 0.1% |
| **Total DigitExtract** | **192.619** | **194.822** | **387.441** | 100.0% |

Each individual degree-63 output tree takes ~21.5–23.9 ms GPU per channel (~45 ms across both channels).
