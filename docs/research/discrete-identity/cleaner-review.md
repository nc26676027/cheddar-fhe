# Discrete cleaner review

Scope: `EvalPolyBatch`, `DiscreteIntegerCleaner`, and `DiscreteIntegerCleanerTest` on the direct five-bit path. This is a read-only review; no production or test files were changed. No GPU run was performed.

## Findings

### [P1] Per-iteration scale rounding changes the fixed points without entering the error bound

`src/extension/DiscreteIntegerCleaner.cpp:65-73` constructs the next cleaning scale by rounding the previous output scale, then `:114-125` overwrites each digit ciphertext's scale metadata with that rounded value before evaluating `h1`. This changes the represented plaintext by the ratio of the old physical scale to the rounded scale. For a bit `b`, the next input is therefore `r*b+r*e`, not `b+e`; `h1(r)` is not equal to 1 when `b=1`. The returned `ideal_integer_error_bound` at `:134-150` applies `(3+2E)E^2` and omits this representation error.

The paper's nearest-integer rule (CCS 2025, Sec. 4.1, pp. 10-11) permits changing the scale only with an explicit reinterpretation `z'=(Delta/Delta')z` and its error included in the ledger. Preserve the actual scale, or add the exact ratio error to every iteration's bound and certificate. The current test starts at the exact integer scale `2^32`; its later scales exceed `2^53`, where every representable finite double is already integral, so it does not exercise a changed scale. A noninteger permitted `extraction_scale` can exercise this path.

### [P1] The exact coefficient certificate does not cover the compiled GPU polynomial

`EvalPolyBatch::Compile` feeds every coefficient through `EvalPolyNode::Compile` (`src/extension/EvalPoly.cpp:913-923,483-526`). The used `Encoder::EncodeConstant` path multiplies coefficient and scale in `double`, rounds the product in `double`, then builds `BigInt` (`src/core/Encode.cpp:257-263`). The direct polynomial certificate bounds the exact binary64 coefficient polynomial, not these differently quantized, scale-dependent RNS constants, and internal Chebyshev split products can have much larger scales than the input. Thus the certificate's `<=1/512` result cannot be attached to the encrypted extractor without a bound for compiled-constant error and its propagation through the actual tree. Record the encoded integer for each node and prove the difference from its intended coefficient, or prepare constants with sufficient precision and certify that path. The sampled endpoint/center test at `unittest/DiscreteIntegerCleanerTest.cpp:53-89` is not a continuum bound for the compiled tree.

### [P1] The required `epsilon_de <= 1/512` contract is not enforced

The constructor accepts any `max_digit_error <= 0.25` at `src/extension/DiscreteIntegerCleaner.cpp:34-42`, and `Evaluate` seeds the recurrence from that caller-supplied value at `:108` without checking it against the required direct extractor contract. The current test happens to pass `2^-9` at `unittest/DiscreteIntegerCleanerTest.cpp:43`, but another caller can construct the same cleaner with `1/4` and still receive a normal-looking state. Enforce `max_digit_error <= 1/512` for this profile and reject a contract whose exact certificate is absent or exceeds it. Keep `max_input_error` tied to the certified manifest rather than treating it as descriptive metadata.

### [P1] Reported mathematical bound excludes all encrypted evaluation error

The header documents this limitation at `include/extension/DiscreteIntegerCleaner.h:30-32`, and the state returned at `src/extension/DiscreteIntegerCleaner.cpp:147-150` exposes the ideal recurrence as if it were the complete integer bound. The paper's cleaning analysis requires an additive per-iteration evaluation term (`eta_r`) for relinearization, rescale, coefficient, and encoding errors. The test only asserts observed decrypted values against fixed thresholds at `unittest/DiscreteIntegerCleanerTest.cpp:82-89,115`; it does not measure or budget those terms. Keep the ideal field explicitly separate, but add a required encrypted-error ledger or avoid using this state as a correctness certificate.

### [P2] Level and operation counts are asserted from a planner, not observed per output

`EvalPolyBatch` assigns depth `Log2Ceil(coefficients.size())` at `src/extension/EvalPoly.cpp:891` and compiles every root at `input_level-depth_` at `:921-923`. The cleaner reports extraction depth 6, cleaning multiplicative depth 4, and rescale count 10 from this planner (`src/extension/DiscreteIntegerCleaner.cpp:147-150`). These are different metrics: each thrifty cleaning uses **two ciphertext products but only one rescale/level**. The test checks the returned numbers at `unittest/DiscreteIntegerCleanerTest.cpp:93-96`, but not the NP/level of every observed extraction and cleaning output against its expected schedule. Assert actual levels at those boundaries; separately count key switches/non-scalar products if claiming the paper's 22-operation extraction cost. `GetRequiredBasisCount()` is only the requested degree-set size, not the number of recursively constructed shared powers (`src/extension/EvalPoly.cpp:919-923`).

## Alias and paper-path checks

No source-alias defect was found on the exercised path. `EvalPolyBatch::Evaluate` copies the input before shared-power evaluation (`src/extension/EvalPoly.cpp:934-945`); cleaner output aliasing is also delayed until after all reads (`src/extension/DiscreteIntegerCleaner.cpp:101-145`). Existing `Context::MultKey` and `ModDown` use temporaries for in-place relinearization/mod-down. The test's final alias case at `unittest/DiscreteIntegerCleanerTest.cpp:98-104` is therefore meaningful, although it does not cover aliasing during every intermediate operation.

The implemented cubic graph has two ciphertext products and one rescale per cleaning iteration (`src/extension/DiscreteIntegerCleaner.cpp:117-125`), consistent with the paper's thrifty structure. Its physical scale and error ledger remain blocked by the P1 rounding issue above. The five-bit signed recombination and `-16` offset are represented by the configured weights and offset constants (`:79-83,136-145`), subject to the same final scale correctness.
