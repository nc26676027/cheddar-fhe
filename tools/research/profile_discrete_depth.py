#!/usr/bin/env python3
"""Run serialized, fresh-process Discrete identity qualification/profiling."""

import argparse
import hashlib
import json
import math
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


def median_ci(values, alpha=0.05):
    ordered = sorted(values)
    n = len(ordered)
    if not n:
        return None
    if n == 1:
        return [ordered[0], ordered[0]]
    # Exact binomial tail for order-statistic 95% CI of the median.
    target = alpha / 2
    cumulative = 0.0
    lower_idx = 0
    for k in range(n + 1):
        cumulative += math.comb(n, k) * (0.5 ** n)
        if cumulative <= target:
            lower_idx = k
        else:
            break
    upper_idx = min(n - 1, n - 1 - lower_idx)
    return [ordered[lower_idx], ordered[upper_idx]]


def summarize_identity19(log):
    text = log.read_text()
    samples = [
        [float(value) for value in line.split(",")[2:]]
        for line in text.splitlines() if line.startswith("MEBOOT_PERF_SAMPLE,")
    ]
    stages = {}
    for line in text.splitlines():
        if line.startswith("MEBOOT_PERF_STAGE,"):
            _, _, _, name, gpu, host = line.split(",")
            stages.setdefault(name, []).append(float(gpu))
    meta = {}
    for line in text.splitlines():
        if line.startswith("MEBOOT_PERF_META,"):
            parts = line.split(",")
            meta = {
                "route": parts[1],
                "slots": int(parts[2]),
                "input_level": int(parts[3]),
                "prepare_ms": float(parts[4]),
                "keygen_ms": float(parts[5]),
                "live_bytes": float(parts[6]),
                "peak_allocated_bytes": float(parts[7]),
            }
        elif line.startswith("MEBOOT_PERF_OUTPUT,"):
            parts = line.split(",")
            meta["output_level"] = float(parts[2])
            meta["output_log_scale"] = float(parts[3])
    gpu = sorted(row[2] for row in samples)
    wall = sorted(row[1] for row in samples)
    return {
        "log": str(log),
        "sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
        "passed": "[  PASSED  ] 1 test." in text and "[  FAILED  ]" not in text,
        **meta,
        "calls": len(samples),
        "gpu_median_ms": statistics.median(gpu) if gpu else None,
        "gpu_ci95_ms": median_ci(gpu),
        "gpu_p95_ms": gpu[max(0, (95 * len(gpu) + 99) // 100 - 1)] if gpu else None,
        "wall_median_ms": statistics.median(wall) if wall else None,
        "wall_ci95_ms": median_ci(wall),
        "stage_gpu_median_ms": {
            name: statistics.median(values) for name, values in stages.items()
        },
        "profile_calls": sum(line.startswith("MEBOOT_PERF_TOTAL,")
                             for line in text.splitlines()),
    }


def query_gpu_state():
    try:
        out = subprocess.check_output(
            ["nvidia-smi",
             "--query-gpu=timestamp,name,driver_version,pstate,clocks.gr,clocks.mem,temperature.gpu,power.draw",
             "--format=csv,noheader"],
            text=True, stderr=subprocess.DEVNULL).strip()
        return out
    except Exception as exc:
        return f"unavailable: {exc}"


def aggregate_discrete(cases):
    from summarize_discrete_depth_campaign import category
    repeats_gpu, repeats_wall = [], []
    group_profiles = []
    basis_ms, tree_ms, offset_ms = [], [], []
    per_tree = {i: [] for i in range(6)}
    for case in cases:
        log = Path(case["log"])
        current = None
        cur_basis, cur_trees, cur_offset = 0.0, 0.0, 0.0
        for line in log.read_text().splitlines():
            if line.startswith("DISCRETE_REPEAT,"):
                parts = line.split(",")
                repeats_gpu.append(float(parts[2]))
                repeats_wall.append(float(parts[3]))
            elif line.startswith("DISCRETE_PROFILE_TOTAL,"):
                if current is not None:
                    group_profiles.append(current)
                    if cur_basis > 0:
                        basis_ms.append(cur_basis)
                        tree_ms.append(cur_trees)
                        offset_ms.append(cur_offset)
                current = {}
                cur_basis, cur_trees, cur_offset = 0.0, 0.0, 0.0
            elif line.startswith("DISCRETE_PROFILE_STAGE,"):
                _, stage, gpu, _ = line.split(",")
                val = float(gpu)
                group = category(stage)
                current[group] = current.get(group, 0.0) + val
                if stage.endswith("/extract/basis"):
                    cur_basis += val
                elif "/extract/tree/" in stage:
                    cur_trees += val
                    idx = int(stage.rsplit("/", 1)[1])
                    if idx in per_tree:
                        per_tree[idx].append(val)
                elif "/extract/" in stage:
                    cur_offset += val
        if current is not None:
            group_profiles.append(current)
            if cur_basis > 0:
                basis_ms.append(cur_basis)
                tree_ms.append(cur_trees)
                offset_ms.append(cur_offset)
    result = {
        "keysets": len(cases),
        "calls": len(repeats_gpu),
        "gpu_median_ms": statistics.median(repeats_gpu),
        "gpu_ci95_ms": median_ci(repeats_gpu),
        "gpu_p95_ms": sorted(repeats_gpu)[max(0, (95 * len(repeats_gpu) + 99) // 100 - 1)],
        "wall_median_ms": statistics.median(repeats_wall),
        "wall_ci95_ms": median_ci(repeats_wall),
        "worst_bits": min(case["worst_bits"] for case in cases),
        "max_clean_integer_error": max(case["clean_integer_error"] for case in cases),
        "peak_allocated_bytes": int(max(case["peak_allocated_bytes"] for case in cases)),
        "resident_evk_bytes": int(max(case["resident_evk_bytes"] for case in cases)),
        "stage_groups_gpu_median_ms": {
            key: statistics.median(g[key] for g in group_profiles)
            for key in group_profiles[0]
        } if group_profiles else {},
        "cases": cases,
    }
    if basis_ms:
        result["digit_extract_breakdown_gpu_median_ms"] = {
            "basis_both_channels": statistics.median(basis_ms),
            "trees_both_channels": statistics.median(tree_ms),
            "offset_add_both_channels": statistics.median(offset_ms),
            "per_tree_single_channel_median_ms": {
                str(i): statistics.median(vals) for i, vals in per_tree.items() if vals
            },
        }
    return result


def run_interleaved(args):
    args.out.mkdir(parents=True, exist_ok=True)
    base_bin = Path(args.baseline_binary).resolve()
    curr_bin = Path(args.binary).resolve()
    id19_bin = Path(args.identity19_binary).resolve()
    candidates = {"pre_cubic": [], "current": [], "identity19": []}
    gpu_snapshots = []
    for key in range(1, args.keysets + 1):
        # Alternate A/B order across rounds to cancel monotonic thermal/clock drift.
        order = [("pre_cubic", base_bin), ("current", curr_bin)]
        if key % 2 == 0:
            order.reverse()
        order.append(("identity19", id19_bin))
        for label, binary in order:
            log = args.out / f"{label}-key{key}.log"
            env = os.environ.copy()
            gpu_before = query_gpu_state()
            if label == "identity19":
                env["LD_LIBRARY_PATH"] = str(curr_bin.parent.parent) + ":" + env.get("LD_LIBRARY_PATH", "")
                env.update({
                    "MEBOOT_PERF": "1",
                    "MEBOOT_PERF_WARMUPS": str(args.warmups),
                    "MEBOOT_PERF_REPEATS": str(args.repetitions),
                    "MEBOOT_PERF_PROFILE": "1",
                    "MEBOOT_PERF_PROFILE_REPEATS": str(args.profiles),
                })
                cmd = [str(binary), "--gtest_filter=MEBootIdentity19.PublicBootstrapPerformance"]
            else:
                lib_dir = base_bin.parent if label == "pre_cubic" else curr_bin.parent.parent
                env["LD_LIBRARY_PATH"] = str(lib_dir) + ":" + env.get("LD_LIBRARY_PATH", "")
                env.update({
                    "DISCRETE_WARMUPS": str(args.warmups),
                    "DISCRETE_REPETITIONS": str(args.repetitions),
                    "DISCRETE_PROFILE_REPETITIONS": str(args.profiles),
                })
                cmd = [str(binary)]
            with log.open("x") as stream:
                res = subprocess.run(cmd, env=env, stdout=stream, stderr=subprocess.STDOUT)
            gpu_after = query_gpu_state()
            gpu_snapshots.append({
                "round": key, "candidate": label,
                "before": gpu_before, "after": gpu_after,
            })
            case = summarize_identity19(log) if label == "identity19" else summarize(log)
            case["returncode"] = res.returncode
            case["gpu_before"] = gpu_before
            case["gpu_after"] = gpu_after
            candidates[label].append(case)
            print(json.dumps({
                "round": key, "candidate": label, "passed": case["passed"],
                "gpu_median_ms": case["gpu_median_ms"],
                "wall_median_ms": case["wall_median_ms"],
            }), flush=True)
            if res.returncode or not case["passed"]:
                raise SystemExit(1)

    pre = aggregate_discrete(candidates["pre_cubic"])
    cur = aggregate_discrete(candidates["current"])
    id19_samples_gpu, id19_samples_wall = [], []
    id19_stages = {}
    for case in candidates["identity19"]:
        for line in Path(case["log"]).read_text().splitlines():
            if line.startswith("MEBOOT_PERF_SAMPLE,"):
                parts = line.split(",")
                id19_samples_wall.append(float(parts[3]))
                id19_samples_gpu.append(float(parts[4]))
            elif line.startswith("MEBOOT_PERF_STAGE,"):
                _, _, _, sname, gpu, _ = line.split(",")
                id19_stages.setdefault(sname, []).append(float(gpu))
    id19 = {
        "keysets": len(candidates["identity19"]),
        "calls": len(id19_samples_gpu),
        "gpu_median_ms": statistics.median(id19_samples_gpu),
        "gpu_ci95_ms": median_ci(id19_samples_gpu),
        "gpu_p95_ms": sorted(id19_samples_gpu)[max(0, (95 * len(id19_samples_gpu) + 99) // 100 - 1)],
        "wall_median_ms": statistics.median(id19_samples_wall),
        "wall_ci95_ms": median_ci(id19_samples_wall),
        "stage_gpu_median_ms": {
            k: statistics.median(v) for k, v in id19_stages.items()
        },
        "cases": candidates["identity19"],
    }
    paired_cubic_deltas = [
        c["gpu_median_ms"] - a["gpu_median_ms"]
        for a, c in zip(candidates["pre_cubic"], candidates["current"])
    ]
    paired_id19_gaps = [
        c["gpu_median_ms"] - i["gpu_median_ms"]
        for c, i in zip(candidates["current"], candidates["identity19"])
    ]
    summary = {
        "schema": 1,
        "protocol": "interleaved ABAB + same-session Identity19 (round odd: A,B,C; round even: B,A,C)",
        "keysets_per_candidate": args.keysets,
        "warmups_per_keyset": args.warmups,
        "repetitions_per_keyset": args.repetitions,
        "profiles_per_keyset": args.profiles,
        "gpu_snapshots": gpu_snapshots,
        "pre_cubic": pre,
        "current": cur,
        "identity19": id19,
        "comparisons": {
            "current_vs_pre_cubic_gpu_median_delta_ms": cur["gpu_median_ms"] - pre["gpu_median_ms"],
            "current_vs_pre_cubic_gpu_median_pct": 100.0 * (cur["gpu_median_ms"] / pre["gpu_median_ms"] - 1.0),
            "per_round_gpu_median_deltas_ms": paired_cubic_deltas,
            "current_vs_identity19_gpu_median_gap_ms": cur["gpu_median_ms"] - id19["gpu_median_ms"],
            "current_vs_identity19_gpu_ratio": cur["gpu_median_ms"] / id19["gpu_median_ms"],
            "per_round_identity19_gaps_ms": paired_id19_gaps,
        },
    }
    out_file = args.out / f"{args.name}-summary.json"
    out_file.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary["comparisons"], indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/unittest/meboot_discrete_identity_test")
    parser.add_argument("--baseline-binary",
                        default="build/discrete-optimization-20261019/baseline-artifacts/meboot_discrete_identity_test")
    parser.add_argument("--identity19-binary",
                        default="build/unittest/meboot_performance_test")
    parser.add_argument("--interleaved", action="store_true")
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
    if args.interleaved:
        run_interleaved(args)
        return
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
