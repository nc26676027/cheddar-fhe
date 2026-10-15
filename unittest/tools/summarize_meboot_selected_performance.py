"""Summarize paired selected MEBOOT latency runs with median/p95 and a
paired 95% confidence interval. Latency only; correctness is qualified by the
meboot_test gates and is deliberately not re-derived here."""
import csv
import json
import statistics
import sys
from pathlib import Path

evidence = Path(sys.argv[1] if len(sys.argv) > 1 else
                "build/validation/meboot-performance")

ROUTES = ("joint4434", "ordinary")
samples = {route: [] for route in ROUTES}      # (run, repeat) -> {wall, gpu}
meta = {}
outputs = {}
stages = {route: {} for route in ROUTES}
totals = {route: [] for route in ROUTES}
commands = []
if (evidence / "commands.tsv").exists():
    commands = (evidence / "commands.tsv").read_text().splitlines()

for log in sorted(evidence.glob("run-*-*.log")):
    run = int(log.name.split("-")[1])
    for line in log.read_text().splitlines():
        row = next(csv.reader([line]))
        if not row:
            continue
        if row[0] == "MEBOOT_PERF_SAMPLE":
            samples[row[1]].append(
                {"run": run, "repeat": int(row[2]),
                 "wall_ms": float(row[3]), "gpu_ms": float(row[4])})
        elif row[0] == "MEBOOT_PERF_META":
            meta[row[1]] = {
                "slots": int(row[2]), "input_level": int(row[3]),
                "prepare_ms": float(row[4]), "keygen_ms": float(row[5]),
                "live_bytes": int(row[6]), "peak_bytes": int(row[7])}
        elif row[0] == "MEBOOT_PERF_OUTPUT":
            outputs[row[1]] = {
                "level": int(row[2]), "log2_scale": float(row[3]),
                "slots": int(row[4])}
        elif row[0] == "MEBOOT_PERF_STAGE":
            stages.setdefault(row[1], {}).setdefault(row[3], []).append(
                {"gpu_ms": float(row[4]), "host_submission_ms": float(row[5])})
        elif row[0] == "MEBOOT_PERF_TOTAL":
            totals.setdefault(row[1], []).append(
                {"wall_ms": float(row[3]), "gpu_ms": float(row[4]),
                 "stage_sum_ms": float(row[5])})


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return None
    index = min(len(ordered) - 1, int(round(fraction * (len(ordered) - 1))))
    return ordered[index]


def stats(route, field):
    rows = samples[route]
    values = [row[field] for row in rows]
    if not values:
        return None
    return {
        "samples": len(values),
        "runs": len({row["run"] for row in rows}),
        "median_ms": statistics.median(values),
        "mean_ms": statistics.fmean(values),
        "p95_ms": percentile(values, 0.95),
        "min_ms": min(values),
        "max_ms": max(values),
        "stdev_ms": statistics.stdev(values) if len(values) > 1 else 0.0,
    }


report = {"evidence_dir": str(evidence), "commands": commands, "routes": {}}
for route in ROUTES:
    report["routes"][route] = {
        "wall": stats(route, "wall_ms"),
        "gpu": stats(route, "gpu_ms"),
        "meta": meta.get(route),
        "output": outputs.get(route),
        "stage_median_gpu_ms": {
            name: statistics.median(row["gpu_ms"] for row in rows)
            for name, rows in stages.get(route, {}).items()},
        "profile_totals": totals.get(route, []),
    }

# Paired comparison: match joint and ordinary by (run, repeat) and difference
# per pair, so host drift between runs cancels rather than inflating variance.
paired = []
joint = {(row["run"], row["repeat"]): row for row in samples["joint4434"]}
ordinary = {(row["run"], row["repeat"]): row for row in samples["ordinary"]}
for key in sorted(set(joint) & set(ordinary)):
    paired.append({
        "run": key[0], "repeat": key[1],
        "wall_delta_ms": joint[key]["wall_ms"] - ordinary[key]["wall_ms"],
        "gpu_delta_ms": joint[key]["gpu_ms"] - ordinary[key]["gpu_ms"],
    })


def paired_ci(field):
    deltas = [row[field] for row in paired]
    if len(deltas) < 2:
        return None
    mean = statistics.fmean(deltas)
    sem = statistics.stdev(deltas) / (len(deltas) ** 0.5)
    half = 1.96 * sem
    return {"pairs": len(deltas), "mean_delta_ms": mean,
            "ci95_low_ms": mean - half, "ci95_high_ms": mean + half}


report["paired_joint_minus_ordinary"] = {
    "wall": paired_ci("wall_delta_ms"),
    "gpu": paired_ci("gpu_delta_ms"),
    "records": paired,
}

# Integration target from the spec: selected joint <= selected ordinary.
joint_wall = report["routes"]["joint4434"]["wall"]
ordinary_wall = report["routes"]["ordinary"]["wall"]
if joint_wall and ordinary_wall:
    report["joint_not_slower_than_ordinary"] = {
        "median_wall_ratio": joint_wall["median_ms"] / ordinary_wall["median_ms"],
        "median_gpu_ratio": (report["routes"]["joint4434"]["gpu"]["median_ms"] /
                             report["routes"]["ordinary"]["gpu"]["median_ms"]),
        "passed": joint_wall["median_ms"] <= ordinary_wall["median_ms"],
    }

out = evidence / "meboot-performance-summary.json"
out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report, indent=2))
