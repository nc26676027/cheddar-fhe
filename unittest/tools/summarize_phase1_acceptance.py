"""Summarize recorded execution, including skips and cryptographic error stats."""
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

repo = Path(__file__).resolve().parents[2]
evidence = repo / ".scratch/phase1-acceptance"
logs = evidence / "logs"
targets = ["meboot_matrix_planner_test", "aks_key_test", "lcr_step_test",
           "meboot_cts_test", "linear_transform_test", "basic_test", "boot_test"]
exit_codes = dict(line.split("\t") for line in
                  (logs / "commands.tsv").read_text().splitlines())
summary = {"binaries": [], "precision": [], "passed": 0, "failed": 0, "skipped": 0}
all_cases = {}
for target in targets:
    root = ET.parse(logs / f"{target}.xml").getroot()
    cases = list(root.iter("testcase"))
    failures = sum(case.find("failure") is not None or case.find("error") is not None
                   for case in cases)
    skips = sum(case.find("skipped") is not None for case in cases)
    passes = sum(case.get("status") == "run" and case.find("skipped") is None
                 and case.find("failure") is None and case.find("error") is None
                 for case in cases)
    row = {"target": target, "tests": len(cases), "passed": passes,
           "failed": failures, "skipped": skips, "seconds": float(root.get("time")),
           "exit_code": int(exit_codes[target])}
    summary["binaries"].append(row)
    for key in ("passed", "failed", "skipped"):
        summary[key] += row[key]
    for case in cases:
        all_cases[f"{target}:{case.get('name')}"] = case

    current_case = ""
    profile = ""
    for line in (logs / f"{target}.log").read_text().splitlines():
        if "[ RUN      ]" in line:
            current_case = line.split("]", 1)[1].strip()
            profile = ""
        if line.startswith(">>>>> "):
            profile = line.removeprefix(">>>>> ").removesuffix(" <<<<<")
        error = re.search(r"Diff magnitude .* range: \[([^,]+), ([^ ]+) \]", line)
        if error:
            summary["precision"].append({"test": current_case, "profile": profile,
                                         "max_complex_error": float(error[2])})
        snr = re.search(r"SNR .* = ([0-9.eE+-]+)", line)
        if snr and summary["precision"]:
            value = float(snr[1])
            summary["precision"][-1]["snr_power_ratio"] = value
            summary["precision"][-1]["snr_db"] = 10 * math.log10(value)

# Successful process exit alone can conceal missing/skipped required coverage.
for target, suffix in [
    ("meboot_cts_test", "MEBootCtSEvaluation/bootparam_40_json"),
    ("meboot_cts_test", "MEBootCtSEvaluation/bootparam_40_64bit_json"),
    ("boot_test", "MEBootBootstrap/bootparam_40_json"),
    ("boot_test", "MEBootBootstrap/bootparam_40_64bit_json"),
]:
    case = all_cases[f"{target}:{suffix}"]
    assert case.get("status") == "run" and case.find("skipped") is None, suffix
assert all(row["exit_code"] == 0 for row in summary["binaries"]), summary["binaries"]
assert summary["failed"] == 0
assert summary["skipped"] == 2, "Only the two unsupported MEBOOT 32-bit profiles may skip"
(evidence / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
for row in summary["binaries"]:
    print(f"{row['target']}: {row['passed']} passed, {row['skipped']} skipped")
print(f"Total: {summary['passed']} passed, {summary['failed']} failed, {summary['skipped']} skipped")
