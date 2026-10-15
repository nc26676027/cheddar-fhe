# Discrete CKKS Identity and MEBOOT Integration

## Objective and immutable constraints

Implement the source-derived Discrete CKKS integer cleaner and identity
bootstrap, then integrate MEBOOT behind a separate selectable research
interface. Preserve the existing Boot() contract, original working tree and
all inherited/frozen evidence. Runtime arithmetic remains Cheddar GPU-native.

Research worktree: D:/code space/cheddar-fhe-discrete-identity
Branch: codex/discrete-identity
Initial snapshot: 94f33e9791e0e312c28fa80fe36f5f9fd0d424f4
Original branch: feat/meboot-cts at d0115641a5f1c7407bce9d16fc9f562f666767a1

## Phases and gates

| Phase | Status | Required gate |
| --- | --- | --- |
| 0. Discovery, preservation and native baseline audit | in_progress | Pin sources/runtime, inventory real kernels, run existing tests before source edits |
| 1. Paper-derived specification | complete | Full primary PDF read; source specification distinguishes missing author constants |
| 2. Shared architecture and backend compatibility | in_progress | Shared GPU cleaner implemented; high-precision helper and identity seam pending |
| 3. Independent plaintext DigitExtract/VecClean | complete | Six known-answer reference tests; full-cell exact-rational certificate for original five-bit profile |
| 4. Encrypted integer cleaning | in_progress | New native test built; waits for serialized baseline suite before GPU execution |
| 5. Complete original Discrete Identity | pending | Complete encrypted baseline passes declared precision and units |
| 6. MEBOOT four-role CtS and entry integration | pending | Only after phase 5; same integer representative and aligned bypass |
| 7. HW40/domain/depth/security validation | pending | Exact current public exposure; conditional domain bound separated from RLWE |
| 8. Complete MEBOOT Discrete Identity | pending | Strictly more than 18 output bits under frozen domain and independent checks |
| 9. Fusion and matched GPU measurements | pending | Both full identity gates first; no CPU fallback; matched contracts |
| 10. Reproducibility and final report | pending | Sources, manifests, raw outputs, ledger, honest A-N status |

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
