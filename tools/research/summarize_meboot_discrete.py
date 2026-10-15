"""Summarize full-slot Discrete Identity gates from independent process logs."""

import argparse
import hashlib
import json
import math
import re
import statistics
from pathlib import Path


NUMBER = r"[-+0-9.eE]+"
TEST = "MEBOOTDiscreteIdentity.FullSlotIdentityAndCommonInteger"


def summarize(paths):
    cases = []
    for path in paths:
        raw = path.read_bytes()
        text = raw.decode("utf-8")
        for index, block in enumerate(text.split("[ RUN      ] " + TEST)[1:]):
            verdict = re.search(r"\[\s+(OK|FAILED)\s+\] " + re.escape(TEST), block)
            if not verdict or verdict[1] != "OK":
                raise ValueError(f"{path}: identity case did not pass")
            metrics = {}
            for name in ("average_bits", "median_bits", "worst_bits", "max_error",
                         "entry_message_error", "clean_integer_error",
                         "transform_message_error", "raised_message_error",
                         "peak_allocated_bytes", "resident_evk_bytes", "alias_max_error"):
                match = re.search(r"\b" + name + "=(" + NUMBER + ")", block)
                if not match:
                    raise ValueError(f"{path}: missing metric {name}")
                metrics[name] = float(match[1])
            repeats = [
                {"repetition": int(row[0]), "gpu_ms": float(row[1]),
                 "wall_ms": float(row[2]), "max_error": float(row[3]),
                 "reported_bits": float(row[4])}
                for row in re.findall(
                    r"DISCRETE_REPEAT,(\d+),(" + NUMBER + "),(" + NUMBER +
                    "),(" + NUMBER + "),(" + NUMBER + ")", block)
            ]
            if len(repeats) != 3:
                raise ValueError(f"{path}: expected three repeated evaluations")
            values = list(metrics.values()) + [
                value for row in repeats for value in row.values()]
            if not all(math.isfinite(value) for value in values):
                raise ValueError(f"{path}: nonfinite result")
            worst = max([metrics["max_error"], metrics["alias_max_error"]] +
                        [row["max_error"] for row in repeats])
            if worst >= 2**-18:
                raise ValueError(f"{path}: maximum complex error fails >18 bits")
            cases.append({
                "log": str(path), "log_sha256": hashlib.sha256(raw).hexdigest(),
                "case_index_in_log": index, "metrics": metrics, "repeats": repeats,
                "maximum_error_including_alias": worst,
                "bits_from_maximum_error": -math.log2(worst),
            })
    if not cases:
        raise ValueError("no complete identity cases found")
    samples = [row for case in cases for row in case["repeats"]]
    return {
        "schema": 1, "independent_process_cases": len(cases), "slots_per_case": 32768,
        "input": {"level": 2, "log_scale": 40},
        "output": {"level": 7, "log_scale": 40},
        "strict_worst_slot_gate_passed": True,
        "worst_bits_across_cases": min(case["bits_from_maximum_error"] for case in cases),
        "median_gpu_ms": statistics.median(row["gpu_ms"] for row in samples),
        "median_wall_ms": statistics.median(row["wall_ms"] for row in samples),
        "peak_tracked_gpu_bytes": int(max(
            case["metrics"]["peak_allocated_bytes"] for case in cases)),
        "timing_scope": (
            "prepared complete GPU Evaluate, same input; observed first call is "
            "warmup; no observers or decode in three timed repetitions; "
            "not a matched benchmark against native MEBOOT or the HW30 baseline"
        ),
        "keyset_scope": (
            "fresh UserInterface per test process; std::random_device-seeded "
            "test PRNG; independent process log invocations supplied by caller"
        ),
        "cases": cases,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logs", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = summarize(args.logs)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in (
        "independent_process_cases", "worst_bits_across_cases",
        "median_gpu_ms", "median_wall_ms")}))
