# Depth/scaling campaign evidence

Base revision: `797761434b9ce5bd0b52a976ff1036f5d75e61c6`.
Current work is in `codex/discrete-identity`; source and binary hashes,
rather than the base commit alone, identify the new implementation.

## Accepted measurements

- `baseline-key{1,2,3}.log`: preserved L7 implementation, matching old
  headers/library, five warmups and thirty timed calls per process.
- `depth18-key{1,2,3}.log`: default retained-chain route, basis 2^59,
  extraction 2^32, one/one cubic rescales, output L18.
- `latency9-key{1,2,3}.log`: alignment L21, basis 2^40, extraction 2^29,
  one/two cubic rescales, output L9.
- `comparison.json`: checked nine-process/270-call comparison, including
  per-case raw-log SHA-256 and both GPU-event and host-clock intervals.
- `measurements.md`: generated latency, precision, memory and stage tables.
- `selected-cleaner-final.log`: four tests, including original cleaner
  entry points and all 64 signed cells with the selected normalized entry.
- `cells-low29-final.log`: all-cell qualification of the L9 alternative.
- `regression-final.log`: twelve inherited targets pass; the complete
  native target reaches its initial 240-second limit.
- `regression-native-retry.log`: full native target rerun with an enlarged
  time allowance, all nine cases pass in 281.92 seconds. Combined with the
  twelve initial passes, all thirteen inherited targets are verified.

Each new route has fifteen instrumented stage profiles, separate from
its ninety unobserved complete calls. All accepted full-slot cases check
strict maximum complex error below 2^-18, integer residual below 2^-33,
finite repeated outputs, aliasing, expected level/scale and stable live
allocator bytes.

## Parameter and build provenance

- `depth18-parameter-audit.json`, `latency9-parameter-audit.json`: source
  hashes, exact Q/P and entry domain, every main-chain prime and its
  bit length/logarithmic magnitude, level/scale/rescale ledgers.
- `direct6-certificate-current.json`: fresh exact-rational full-cell
  coefficient certificate against current manifest bytes.
- `build-bindings.json`: compiled library/executable hashes, preserved
  baseline harness/header hashes, CMake flags and source/runtime fixture
  equality checks.
- `gpu-environment.txt`: driver, CUDA, compiler, WSL and GPU metadata.
- `regression-configuration.json`: test scope and timeout settings.

The inherited certificate's manifest hash refers to CRLF bytes. Current
LF bytes reproduce that old hash when converted to CRLF, and the fresh
checker produces identical mathematical bounds with a current-byte hash.
Neither historical evidence nor the coefficient values are modified.

## Candidate and excluded evidence

The failed candidates are intentional qualification results:

| Candidate | Result |
| --- | --- |
| high33/high34, one/two rescales | Fail integer and full-identity precision gates |
| high35-trial, one/two rescales | Pass L17 smoke test; superseded by L18 |
| candidate-high36-12 | Pass L17 pilot; not included in matched timing |
| high32-11 | Pass L18 pilot; followed by three-process qualification |
| low28, one/two rescales | Fail integer and full-identity gates |
| low29-trial | Pass L9 smoke test; followed by three-process qualification |
| latency10, low extraction 2^26, one/one rescales | Fail full identity despite narrowly passing cell test |
| cells-high30/31/32 | Pass all-cell tests; 2^30/2^31 have no full-route minimum claim |
| cells-low24/25 | Fail all-cell precision gates |

Infrastructure/preflight records do not contribute to accepted results:

- `before-key1.log` and `excluded-before-backtrace.log`: the first baseline
  harness used mismatched headers/library and faulted on destruction.
  The accepted baseline was rebuilt against isolated matching headers.
- `cells-low26.log`: loader error during relinking, replaced by
  `cells-low26-stable.log`. That stable cell pass did not qualify the
  later failing `latency10` full identity.
- `selected-cleaner.log`: earlier one/two-rescale preflight; use
  `selected-cleaner-final.log` for the selected one/one defaults.
- `campaign-jobs.json` records the rejected L10 attempt.
  `campaign-followup-jobs.json` records the accepted follow-up campaign.
  Its final nonzero return code belongs to the initial native timeout;
  `regression-native-retry.log` and `final-integrity.json` resolve that
  regression result without rewriting the original failure log.

## Reproduce

Build the current three Discrete test targets in the existing Release
SM120 WSL build. Keep GPU jobs serialized. Use a new directory because the
profiling runner refuses to overwrite raw logs.

```
build/unittest/discrete_integer_cleaner_test

python3 tools/research/profile_discrete_depth.py \
  --out NEW-CAMPAIGN --name depth18

python3 tools/research/profile_discrete_depth.py \
  --out NEW-CAMPAIGN --name latency9 \
  --alignment 21 --basis 40 --extraction 29 --rescales 12

ctest --test-dir build --output-on-failure --timeout 480 \
  -E '^(basic_test|discrete_integer_cleaner_test|meboot_discrete_identity_test)$'
```

The old implementation must use its matching headers/library; locally
these are preserved under `build/research/depth-before/`.
The old harness source is hash-bound in `build-bindings.json`.
After generating three baseline logs in the new directory:

```
python3 tools/research/summarize_discrete_depth_campaign.py \
  --evidence NEW-CAMPAIGN --output NEW-CAMPAIGN/comparison.json \
  --markdown NEW-CAMPAIGN/measurements.md

python3 tools/research/certify_discrete_extractor.py \
  --manifest parameters/research/discrete_direct6_d95.json \
  --output NEW-CAMPAIGN/direct6-certificate-current.json

python3 tools/research/audit_meboot_discrete.py \
  --profile-log NEW-CAMPAIGN/depth18-key1.log \
  --certificate NEW-CAMPAIGN/direct6-certificate-current.json \
  --output NEW-CAMPAIGN/depth18-parameter-audit.json
```

`basic_test` is a known inherited timeout and is not counted as a pass.
The opt-in native performance test can skip its case; the CTest target's
successful exit does not mean that separate benchmark was executed.
