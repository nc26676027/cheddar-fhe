"""Summarize paired benchmark records; keep precision/depth alongside latency."""
import csv
import json
from pathlib import Path
import statistics
import sys

log = Path(sys.argv[1])
samples = {}
precision = {}
stages = {}
key_payload = {}
levels = {}
for line in log.read_text().splitlines():
    row = next(csv.reader([line]))
    if row and row[0] == "PERF_SAMPLE":
        bits, path = int(row[1]), row[2]
        samples.setdefault((bits, path), []).append(
            {"wall_ms": float(row[4]), "gpu_ms": float(row[5])})
    elif row and row[0] == "PERF_PRECISION":
        precision[(int(row[1]), row[2])] = {
            "max_complex_error": float(row[3]), "snr_db": float(row[4])}
    elif row and row[0] == "PERF_STAGE":
        stages.setdefault((int(row[1]), row[2]), []).append(float(row[4]))
    elif row and row[0] == "PERF_KEYS":
        key_payload[(int(row[1]), row[2])] = int(row[3])
    elif row and row[0] == "PERF_LEVELS":
        values = [int(value) for value in row[2:]]
        levels[int(row[1])] = dict(zip(
            ("key_ceiling", "prefix_start", "standard_cts_end", "meboot_cts_end",
             "standard_output", "meboot_output"), values))

report = []
for bits in sorted({bits for bits, _ in samples}):
    paths = {}
    for path in ("standard", "meboot"):
        rows = samples[(bits, path)]
        wall = [row["wall_ms"] for row in rows]
        paths[path] = {
            "samples": len(rows), "median_wall_ms": statistics.median(wall),
            "min_wall_ms": min(wall), "max_wall_ms": max(wall),
            "median_gpu_ms": statistics.median(row["gpu_ms"] for row in rows),
            **precision[(bits, path)],
            "used_key_payload_bytes": key_payload.get((bits, path)),
        }
    baseline, candidate = paths["standard"], paths["meboot"]
    report.append({
        "word_bits": bits, "paths": paths,
        "levels": levels.get(bits),
        "latency_change_percent": 100 *
            (candidate["median_wall_ms"] / baseline["median_wall_ms"] - 1),
        "snr_change_db": candidate["snr_db"] - baseline["snr_db"],
        "precision_alarm_passed": candidate["snr_db"] >= baseline["snr_db"] - 1,
        "absolute_precision_passed": min(candidate["snr_db"], baseline["snr_db"]) >=
                                     (128 if bits == 32 else 114),
        "basic_latency_target_passed": candidate["median_wall_ms"] <= baseline["median_wall_ms"],
        "median_stage_gpu_ms": {
            name: statistics.median(values)
            for (stage_bits, name), values in stages.items() if stage_bits == bits},
    })
if not report:
    raise RuntimeError("No completed paired benchmark records")
output = log.with_suffix(".summary.json")
output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report, indent=2))
if "--require-targets" in sys.argv[2:]:
    assert {row["word_bits"] for row in report} == {32, 64}, "Both word types must finish"
    assert all(row["precision_alarm_passed"] and row["absolute_precision_passed"] and
               row["basic_latency_target_passed"]
               for row in report), "Precision or Basic latency target failed"
