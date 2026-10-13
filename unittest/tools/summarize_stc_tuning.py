"""Compare fixed-input StC plans using alternating timing samples."""
import csv
import json
from pathlib import Path
import statistics
import sys

log = Path(sys.argv[1])
samples, plans = {}, {}
for line in log.read_text().splitlines():
    row = next(csv.reader([line]))
    if row and row[0] == "STC_SAMPLE":
        key = int(row[1]), int(row[2])
        samples.setdefault(key, []).append((float(row[4]), float(row[5])))
    elif row and row[0] == "STC_PLAN":
        key = int(row[1]), int(row[2])
        names = ["stage", "level", "num_q", "num_p", "active_beta", "diagonals", "baby_steps", "giant_steps"]
        plans.setdefault(key, []).append(dict(zip(names, map(int, row[3:]))))

report = []
for key, values in sorted(samples.items()):
    bits, config = key
    baseline = statistics.median(v[1] for v in samples[(bits, 0)])
    gpu = statistics.median(v[1] for v in values)
    report.append({"word_bits": bits, "config": config, "samples": len(values),
                   "median_wall_ms": statistics.median(v[0] for v in values),
                   "median_gpu_ms": gpu, "gpu_change_percent": 100 * (gpu / baseline - 1),
                   "stages": plans[key]})
assert len(report) == 6 and all(r["samples"] == 30 for r in report)
log.with_suffix(".summary.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
