#!/usr/bin/env python3
"""Run serialized, fresh-process Discrete identity qualification/profiling."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess


def summarize(log):
    text = log.read_text()
    repeats = [
        [float(value) for value in line.split(",")[1:]]
        for line in text.splitlines() if line.startswith("DISCRETE_REPEAT,")
    ]
    stages = {}
    for line in text.splitlines():
        if line.startswith("DISCRETE_PROFILE_STAGE,"):
            _, name, gpu, wall = line.split(",")
            stages.setdefault(name, []).append(float(gpu))
    fields = {}
    for key in ("clean_integer_error", "average_bits", "worst_bits", "max_error",
                "peak_allocated_bytes", "resident_evk_bytes", "alias_max_error",
                "output_level", "basis_bits", "extraction_bits", "extractor_baby_step"):
        matches = re.findall(r"\b" + key + r"=([0-9.eE+-]+)", text)
        if matches:
            fields[key] = float(matches[-1])
    if "output_level" not in fields:
        match = re.search(r"stage=output level=(\d+)", text)
        if match:
            fields["output_level"] = int(match[1])
    if repeats:
        gpu = sorted(row[1] for row in repeats)
        wall = sorted(row[2] for row in repeats)
        fields.update(
            calls=len(repeats),
            gpu_median_ms=statistics.median(gpu),
            gpu_p95_ms=gpu[max(0, (95 * len(gpu) + 99) // 100 - 1)],
            wall_median_ms=statistics.median(wall),
        )
    return {
        "log": str(log),
        "sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
        "passed": "[  PASSED  ] 1 test." in text and "[  FAILED  ]" not in text,
        **fields,
        "stage_gpu_median_ms": {
            name: statistics.median(values) for name, values in stages.items()
        },
        "profile_calls": sum(line.startswith("DISCRETE_PROFILE_TOTAL,")
                             for line in text.splitlines()),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/unittest/meboot_discrete_identity_test")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--name", required=True)
    parser.add_argument("--keysets", type=int, default=3)
    parser.add_argument("--repetitions", type=int, default=30)
    parser.add_argument("--warmups", type=int, default=5)
    parser.add_argument("--profiles", type=int, default=5)
    parser.add_argument("--basis", type=int)
    parser.add_argument("--extraction", type=int)
    parser.add_argument("--alignment", type=int)
    parser.add_argument("--rescales", choices=("11", "12", "21", "22"))
    parser.add_argument("--legacy-d95", action="store_true")
    parser.add_argument("--baby-step", type=int)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.pop("DISCRETE_LEGACY_D95", None)
    if args.legacy_d95:
        env["DISCRETE_LEGACY_D95"] = "1"
    options = {
        "DISCRETE_WARMUPS": args.warmups,
        "DISCRETE_REPETITIONS": args.repetitions,
        "DISCRETE_PROFILE_REPETITIONS": args.profiles,
        "DISCRETE_BASIS_BITS": args.basis,
        "DISCRETE_EXTRACTION_BITS": args.extraction,
        "DISCRETE_ALIGNMENT_LEVEL": args.alignment,
        "DISCRETE_CLEANING_RESCALES": args.rescales,
        "DISCRETE_EXTRACTOR_BABY_STEP": args.baby_step,
    }
    env.update({key: str(value) for key, value in options.items() if value is not None})
    evidence = {"command": vars(args).copy(), "environment": options, "cases": []}
    evidence["command"]["out"] = str(args.out)
    failed = False
    for key in range(1, args.keysets + 1):
        log = args.out / f"{args.name}-key{key}.log"
        # Existing evidence is immutable, including failed candidates.
        with log.open("x") as stream:
            result = subprocess.run([args.binary], env=env, stdout=stream,
                                    stderr=subprocess.STDOUT)
        case = summarize(log)
        case["returncode"] = result.returncode
        evidence["cases"].append(case)
        summary = args.out / f"{args.name}-summary.json"
        summary.write_text(json.dumps(evidence, indent=2) + "\n")
        print(json.dumps({key: value for key, value in case.items()
                          if key != "stage_gpu_median_ms"}), flush=True)
        if result.returncode or not case["passed"]:
            failed = True
            break
    raise SystemExit(int(failed))


if __name__ == "__main__":
    main()
