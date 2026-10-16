#!/usr/bin/env python3
"""Validate and aggregate the matched Discrete depth/scaling campaign."""

import argparse
import json
import math
from pathlib import Path
import statistics

from profile_discrete_depth import summarize


def percentile95(values):
    ordered = sorted(values)
    return ordered[(95 * len(ordered) + 99) // 100 - 1]


def category(name):
    if name == "entry":
        return "StC and entry"
    if name.startswith("raised-"):
        return "Lift and sparse-to-dense"
    if name == "low-cts":
        return "MEBOOT low CtS"
    if "/extract/" in name:
        return "DigitExtract, both channels"
    if "/clean/" in name:
        return "Cleaning, both channels"
    if "/combine/" in name or name == "clean-integer":
        return "Integer recombination"
    if name.startswith("cleaner-input-"):
        return "Split and affine alignment"
    if name == "high-cts":
        return "Accurate bypass and alignment"
    if name == "output":
        return "Subtract and normalize"
    return "Temporary release"


def collect(directory, name, expected_level):
    cases, repeats, stage_profiles = [], [], []
    for key in range(1, 4):
        log = directory / f"{name}-key{key}.log"
        case = summarize(log)
        if not case["passed"] or case["output_level"] != expected_level:
            raise ValueError(f"{log}: qualification or output-level mismatch")
        if case["clean_integer_error"] >= 2**-33:
            raise ValueError(f"{log}: integer residual fails")
        for metric in ("max_error", "alias_max_error"):
            if not math.isfinite(case[metric]) or case[metric] >= 2**-18:
                raise ValueError(f"{log}: {metric} fails")
        current, rows = None, []
        for line in log.read_text().splitlines():
            if line.startswith("DISCRETE_REPEAT,"):
                row = [float(value) for value in line.split(",")[1:]]
                if not all(math.isfinite(value) for value in row):
                    raise ValueError(f"{log}: non-finite measurement")
                if row[1] <= 0 or row[2] <= 0 or row[3] >= 2**-18:
                    raise ValueError(f"{log}: repeated timing or precision fails")
                rows.append(row)
            elif line.startswith("DISCRETE_PROFILE_TOTAL,"):
                current = {}
                stage_profiles.append(current)
            elif line.startswith("DISCRETE_PROFILE_STAGE,") and name != "baseline":
                _, stage, gpu, _ = line.split(",")
                if not math.isfinite(float(gpu)) or float(gpu) < 0:
                    raise ValueError(f"{log}: invalid GPU stage interval")
                group = category(stage)
                current[group] = current.get(group, 0) + float(gpu)
        if len(rows) != 30 or [row[0] for row in rows] != list(range(30)):
            raise ValueError(f"{log}: expected exactly 30 indexed timed calls")
        if case["profile_calls"] != (1 if name == "baseline" else 5):
            raise ValueError(f"{log}: wrong number of stage profiles")
        cases.append(case)
        repeats.extend(rows)
    gpu, wall = [row[1] for row in repeats], [row[2] for row in repeats]
    result = {
        "output_level": expected_level,
        "processes": 3, "timed_calls": len(repeats), "slots": 32768,
        "gpu_median_ms": statistics.median(gpu),
        "gpu_p95_ms": percentile95(gpu),
        "gpu_range_ms": [min(gpu), max(gpu)],
        "wall_median_ms": statistics.median(wall),
        "wall_p95_ms": percentile95(wall),
        "wall_range_ms": [min(wall), max(wall)],
        "cuda_event_gt_wall_samples": sum(g > w for g, w in zip(gpu, wall)),
        "worst_bits": -math.log2(max(
            [case[metric] for case in cases for metric in ("max_error", "alias_max_error")]
            + [row[3] for row in repeats])),
        "average_bits_range": [min(case["average_bits"] for case in cases),
                               max(case["average_bits"] for case in cases)],
        "max_integer_error": max(case["clean_integer_error"] for case in cases),
        "peak_pool_bytes": int(max(case["peak_allocated_bytes"] for case in cases)),
        "resident_evk_bytes": int(max(case["resident_evk_bytes"] for case in cases)),
        "cases": cases,
    }
    if name != "baseline":
        expected = {
            "StC and entry", "Lift and sparse-to-dense", "MEBOOT low CtS",
            "DigitExtract, both channels", "Cleaning, both channels",
            "Integer recombination", "Split and affine alignment",
            "Accurate bypass and alignment", "Subtract and normalize",
            "Temporary release",
        }
        if any(set(profile) != expected for profile in stage_profiles):
            raise ValueError(f"{name}: incomplete instrumented stage profile")
        groups = set().union(*(profile.keys() for profile in stage_profiles))
        result["stage_gpu_median_ms"] = {
            group: statistics.median(profile[group] for profile in stage_profiles)
            for group in sorted(groups)
        }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--markdown", type=Path, required=True)
    args = parser.parse_args()
    configurations = {
        name: collect(args.evidence, name, level)
        for name, level in (("baseline", 7), ("depth18", 18), ("latency9", 9))
    }
    baseline = configurations["baseline"]
    for name in ("depth18", "latency9"):
        case = configurations[name]
        case["gpu_change_percent"] = 100 * (
            case["gpu_median_ms"] / baseline["gpu_median_ms"] - 1)
        case["wall_change_percent"] = 100 * (
            case["wall_median_ms"] / baseline["wall_median_ms"] - 1)
    result = {
        "schema": 1, "qualified": True,
        "timing_scope": "complete unobserved Evaluate; prepared keys; decrypt outside timers",
        "comparison_scope": "same fixed message grid and public Q/P; different output levels",
        "warmups_per_process": 5,
        "clock_note": (
            "Some CUDA-event intervals exceed host steady-clock intervals. "
            "Both clocks are preserved; no small speedup is inferred from their difference."),
        "configurations": configurations,
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    lines = [
        "# Measured Discrete depth/scaling comparison", "",
        "Three fresh processes per configuration; five warmups and thirty",
        "unobserved, precision-checked calls per process. RTX 5060 Ti,",
        "WSL Ubuntu, CUDA 12.8, Release SM120. Key generation and decrypt",
        "are outside the timers. Output scale is 2^40 in every row.", "",
        "| Configuration | Output | GPU median ms | GPU p95 ms | Host median ms | Worst slot bits | Pool peak GiB |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for name, case in configurations.items():
        lines.append(
            f"| {name} | L{case['output_level']} | {case['gpu_median_ms']:.3f} | "
            f"{case['gpu_p95_ms']:.3f} | {case['wall_median_ms']:.3f} | "
            f"{case['worst_bits']:.4f} | {case['peak_pool_bytes']/2**30:.4f} |")
    lines.extend(["", "Pool peaks are tracked allocator bytes, not total device usage.",
                  f"Resident evaluation-key arrays: {baseline['resident_evk_bytes']} bytes",
                  f"({baseline['resident_evk_bytes']/2**30:.4f} GiB), unchanged.", "",
                  "| Instrumented stage | L18 median GPU ms | L9 median GPU ms |",
                  "| --- | ---: | ---: |"])
    stages = configurations["depth18"]["stage_gpu_median_ms"]
    for stage in stages:
        lines.append(
            f"| {stage} | {stages[stage]:.3f} | "
            f"{configurations['latency9']['stage_gpu_median_ms'][stage]:.3f} |")
    lines.extend([
        "", "Stage rows aggregate fifteen instrumented profiles per new configuration.",
        "They include event-boundary overhead and must be distinguished from",
        "the unobserved calls in the first table. Group medians need not sum",
        "to the median total. Extraction and cleaning include both channels",
        "and all six digits. Lift and sparse-to-dense are grouped because",
        "the existing observer emits their two marks after both operations.", "",
        result["clock_note"], "",
        "These configurations return different output levels; the comparison",
        "does not establish an equal-level speedup over native Identity19.", "",
    ])
    args.markdown.write_text("\n".join(lines))
    print(json.dumps({name: {key: value for key, value in case.items()
                            if key not in ("cases", "stage_gpu_median_ms")}
                      for name, case in configurations.items()}, indent=2))


if __name__ == "__main__":
    main()
