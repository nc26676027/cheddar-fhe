# Identity19 MEBOOT tools

These tools use the frozen Identity19 MEBOOT profile and keep generated validation output under build/validation.

## Provenance

- parameters/reference/selected-original.json is the immutable selected export.
- Original artifact paths and digests are historical provenance; active fixture paths are recorded in docs/evidence/identity19-baseline/manifest.json.
- export_selected_original.go, export_meboot_stage_oracle.go, and export_stc_quantization_original.go preserve the reference generators.
- unittest/fixtures/identity19-stage-oracle.json.gz is the compressed stage oracle; hashes are in docs/evidence/identity19-baseline/manifest.json.
- import_boot_parameters.py regenerates the production BootParameter source and parameter JSON files. Run it only as part of the main source migration.

## Validation

    python3 unittest/tools/audit_identity19.py
    python3 unittest/tools/audit_identity19.py --security-package ../paper1-linear-bootstrap/paper/evidence/measurements/submission-security-20261007 --output build/validation/identity19-security.json
    bash unittest/tools/run_meboot_diagnostics.sh
    bash unittest/tools/run_meboot_performance.sh

Diagnostics use MEBOOT_STAGE_ORACLE and target meboot_test. Performance uses MEBOOT_PERF_* and targets meboot_performance_test. Both write logs below build/validation.
The security package option checks the current exact domains against the pinned
classical estimator records; the default audit checks parameters and the
conditional domain-escape model only. Historical upstream configuration names
are preserved. Neither command performs a new lattice-estimator run.

Useful focused tools include `audit_identity19.py`, `meboot_lcr_bound.py`,
`analyze_meboot_stages.py`, `export_meboot_golden.go`, and
`export_meboot_stage_oracle.go`.
