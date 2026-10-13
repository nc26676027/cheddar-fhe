"""Summarize continuous BootImpl event intervals and instrumentation overhead."""
import csv
import json
from pathlib import Path
import statistics
import sys

log = Path(sys.argv[1])
totals, stages, baseline = {}, {}, {}
sample_sums = {}
for line in log.read_text().splitlines():
    row = next(csv.reader([line]))
    if not row:
        continue
    if row[0] == "PERF_SAMPLE":
        baseline.setdefault((int(row[1]), row[2]), []).append(
            (float(row[4]), float(row[5])))
    elif row[0] == "BOOT_PROFILE_TOTAL":
        key = (int(row[1]), row[2])
        repeat = int(row[3])
        wall, gpu = float(row[4]), float(row[5])
        totals.setdefault(key, []).append((repeat, wall, gpu))
    elif row[0] == "BOOT_PROFILE_STAGE":
        key = (int(row[1]), row[2])
        repeat, name = int(row[3]), row[4]
        gpu, submission = float(row[5]), float(row[6])
        stages.setdefault(key, {}).setdefault(name, []).append((gpu, submission))
        sample_sums[(key, repeat)] = sample_sums.get((key, repeat), 0) + gpu

assert {key[0] for key in totals} == {32, 64}, "Both word types must finish"
report = []
for key in sorted(totals):
    bits, path = key
    rows = totals[key]
    wall = [r[1] for r in rows]
    gpu = [r[2] for r in rows]
    assert len(rows) == len(baseline[key])
    assert len({r[0] for r in rows}) == len(rows), "Duplicate profile sample"
    residual = max(abs(sample_sums[(key, r[0])] - r[2]) for r in rows)
    assert residual < 0.02, "Stage intervals do not account for total"
    mean_gpu = statistics.mean(gpu)
    baseline_wall = statistics.median(r[0] for r in baseline[key])
    baseline_gpu = statistics.median(r[1] for r in baseline[key])
    stage_rows = []
    for name, values in stages[key].items():
        assert len(values) == len(rows), f"Missing {name} samples"
        mean = statistics.mean(v[0] for v in values)
        stage_rows.append({
            "name": name, "samples": len(values),
            "mean_gpu_ms": mean,
            "median_gpu_ms": statistics.median(v[0] for v in values),
            "gpu_share_percent": 100 * mean / mean_gpu,
            "mean_host_submission_ms": statistics.mean(v[1] for v in values),
        })
    report.append({
        "word_bits": bits, "path": path, "samples": len(rows),
        "mean_gpu_ms": mean_gpu, "median_gpu_ms": statistics.median(gpu),
        "mean_wall_ms": statistics.mean(wall), "median_wall_ms": statistics.median(wall),
        "unprofiled_median_gpu_ms": baseline_gpu,
        "unprofiled_median_wall_ms": baseline_wall,
        "instrumentation_gpu_change_percent": 100 * (statistics.median(gpu) / baseline_gpu - 1),
        "instrumentation_wall_change_percent": 100 * (statistics.median(wall) / baseline_wall - 1),
        "max_accounting_residual_ms": residual, "stages": stage_rows,
    })

assert len(report) == 4, "Both paths of both word types required"
output = log.with_suffix(".profile.json")
output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report, indent=2))
