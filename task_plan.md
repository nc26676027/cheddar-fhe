# Discrete CKKS Identity and MEBOOT Integration

## Objective and immutable constraints

Implement the source-derived Discrete CKKS integer cleaner and identity
bootstrap, then integrate MEBOOT behind a separate selectable library
extension interface. Preserve the existing Boot() contract, original working tree and
all inherited/frozen evidence. Runtime arithmetic remains Cheddar GPU-native.

Research worktree: D:/code space/cheddar-fhe-discrete-identity
Branch: codex/discrete-identity
Initial snapshot: 94f33e9791e0e312c28fa80fe36f5f9fd0d424f4
Original branch: feat/meboot-cts at d0115641a5f1c7407bce9d16fc9f562f666767a1

## Phases and gates

| Phase | Status | Required gate |
| --- | --- | --- |
| 0. Discovery, preservation and native baseline audit | complete | Pinned runtime and kernel inventory; pre-edit regression recorded with known basic_test timeout |
| 1. Paper-derived specification | complete | Full primary PDF read; source specification distinguishes missing author constants |
| 2. Shared architecture and backend compatibility | complete | Shared cleaner and both identity seams implemented; host precision helper tested; no 81-bit claim |
| 3. Independent plaintext DigitExtract/VecClean | complete | Six known-answer reference tests; full-cell exact-rational certificate for original five-bit profile |
| 4. Encrypted integer cleaning | complete | Commit 19c5cb2: 2/2 GPU tests pass; integer precision 30.1 bits |
| 5. Complete original Discrete Identity | complete | Commit d06720c: 32768 GPU slots; average 14.92, median 15.23, worst 11.93 bits |
| 6. MEBOOT four-role CtS and entry integration | complete | Shared lift fork, six-bit cleaner, native bypass with physical alignment; first full-slot pass |
| 7. HW40/domain/depth/security validation | complete | Full-cell exact certificate; actual depth 7; exact public-domain reuse audit; stated model limits |
| 8. Complete MEBOOT Discrete Identity | complete | Three fresh keysets, all 32768 slots: worst 18.8756-18.9616 bits; repeated, finite-output, alias and live-allocation gates pass |
| 9. Fusion and matched GPU measurements | pending | Solo pilot timings recorded; matched precision/level/security comparison and full-circuit fusion ablation remain |
| 10. Reproducibility and final report | in_progress | Current milestone has source/parameter hashes, raw three-keyset logs, summary and A-N report; final comparative report awaits phase 9 |

## Fixed execution rules

- New evidence: docs/evidence/discrete-identity/ only; inherited evidence is read-only.
- Downloaded PDFs and bulky generated files: build/research/ only.
- No ReLU/sign/absolute-value/CNN work.
- No security or precision threshold relaxation after observing results.
- Six extracted bits do not imply six levels; derive depth from the actual graph.
- No MEBOOT integration before the complete original encrypted identity gate.
- Large campaigns wait for small encrypted correctness gates.
- Re-read this plan before changing phase or implementation scope.

## Errors and unresolved items

| Item | Attempt | Resolution/status |
| --- | --- | --- |
| Primary ePrint PDF returned HTTP 403 to web tool | 1 | Locate an author-hosted copy or fetch the primary PDF directly; provenance remains required |
| Original basic_test previously exceeded 12 minutes | historical | Inspect test inventory and run bounded preflight; do not call the prior interruption a pass |
| Host urllib arXiv certificate verification failed | 2 | WSL curl with trusted CA store acquired the original PDF; TLS verification retained |
| Host console GBK could not print mathematical PDF text | 1 | Set PYTHONIOENCODING=utf-8 for the extraction command |
| Original BasicTest exceeds 600 seconds even without timing warmups | 1 | Record timeout, retain passing individual cases; continue remaining serialized regression targets |
| Unbounded sampled minimax LP returned HiGHS unset status | 1 | Add an explicit finite coefficient bound of 1024; independent exact-rational interval certificate checks result |
| New make target absent before CMake reconfiguration | 1 | Reconfigure the isolated build before building the added cleaner test target |
| Handoff plans predate successful phases 4/5 | 1 | Reconciled against clean d06720c and committed encrypted evidence |
| WSL system Python lacks scipy; Windows python is an App Execution Alias | 1 | Use explicit bundled Python executable and existing local dependencies |
| validate_discrete_extractor.py does not exist | 1 | Existing exact checker is certify_discrete_extractor.py |
| Six-bit dense cleaner at input/extraction scale 2^40 has only 25.25 bits | 1 | Kept gate; honored extraction_scale=2^43 with input basis 2^40; 37.20-bit integer pass |
| Raising input basis scale to 2^43 causes extraction failure | 1 | Separate power-basis and extractor-output scales; retained failed evidence |
| Custom bypass starting at L20 / L23 fails >18-bit worst-slot gate | 2 | Independent oracle isolated transform error; native BootTransforms::Bypass plus explicit gain/scale alignment passes at 18.93 bits |
| Plain oracle overload cannot deduce derived BootContext pointer | 2 | Supply a ConstContextPtr<uint64_t> to choose the documented overload |
| Audit used extra_q instead of frozen manifest's proper_extra_q | 1 | Matched exact manifest field; audit now passes |
