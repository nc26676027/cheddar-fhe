"""Aggregate the immutable logs; run from the repository root."""

import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path("tools/research").resolve()))
from profile_discrete_depth import summarize
from summarize_discrete_depth_campaign import collect


directory = Path(__file__).resolve().parent
configurations = {
    "d95": collect(directory, "d95", 19),
    "d63_auto8": collect(directory, "d63", 20),
    "selected_b16": collect(directory, "selected", 20),
}
baseline = configurations["d95"]
for case in configurations.values():
    case["gpu_change_percent_vs_d95"] = 100 * (
        case["gpu_median_ms"] / baseline["gpu_median_ms"] - 1)
result = {
    "schema": 1,
    "timing_scope": "prepared-key Evaluate; decoding and key generation outside timers",
    "scope": "same public Q/P and fixed message grid; independent fresh keys",
    "build_bindings": ["profile-build-bindings.json", "selected-build-bindings.json"],
    "clock_note": "CUDA events and host clocks are retained separately; GPU-event intervals can exceed host intervals on this platform",
    "configurations": configurations,
}
legacy_check = directory / "legacy-final-key1.log"
if legacy_check.exists():
    check = summarize(legacy_check)
    if not check["passed"] or check["output_level"] != 19:
        raise ValueError("final-build legacy confirmation failed")
    result["same_final_build_legacy_confirmation"] = check
selected_check = directory / "selected-final-key1.log"
if selected_check.exists():
    check = summarize(selected_check)
    if not check["passed"] or check["output_level"] != 20:
        raise ValueError("adjacent final-build selected confirmation failed")
    result["same_final_build_selected_confirmation"] = check
    result["adjacent_final_build_gpu_change_percent"] = 100 * (
        check["gpu_median_ms"] /
        result["same_final_build_legacy_confirmation"]["gpu_median_ms"] - 1)
(directory / "comparison.json").write_text(json.dumps(result, indent=2) + "\n")

lines = [
    "# Remez performance measurements", "",
    "RTX 5060 Ti, CUDA 12.8, WSL, Release. Each row has three fresh",
    "keysets, five warmups, 30 checked timed calls per keyset, and five",
    "separate instrumented profiles. Output scale is 2^40.", "",
    "| Configuration | Output | GPU median ms | GPU p95 ms | Host median ms | Worst bits | Pool peak GiB |",
    "|---|---|---:|---:|---:|---:|---:|",
]
for name, case in configurations.items():
    lines.append(
        f"| {name} | L{case['output_level']} | {case['gpu_median_ms']:.3f} | "
        f"{case['gpu_p95_ms']:.3f} | {case['wall_median_ms']:.3f} | "
        f"{case['worst_bits']:.4f} | {case['peak_pool_bytes']/2**30:.4f} |")
lines.extend([
    "", "Allocator peaks exclude total device/driver usage. Evaluation-key arrays:",
    f"{baseline['resident_evk_bytes']} bytes ({baseline['resident_evk_bytes']/2**30:.4f} GiB), unchanged.",
    "", "| Instrumented stage | d95 GPU median ms | selected b16 GPU median ms |",
    "|---|---:|---:|",
])
for stage in baseline["stage_gpu_median_ms"]:
    lines.append(
        f"| {stage} | {baseline['stage_gpu_median_ms'][stage]:.3f} | "
        f"{configurations['selected_b16']['stage_gpu_median_ms'][stage]:.3f} |")
lines.extend([
    "", "Grouped stage medians need not sum to the total median.",
    "The initial d95/auto8 runs use the initial binary; selected b16 uses",
    "the final binary with an explicit baby-step option. Default legacy",
    "decomposition is preserved; binary hashes are recorded separately.",
])
if "same_final_build_legacy_confirmation" in result:
    check = result["same_final_build_legacy_confirmation"]
    lines.extend([
        "", "An additional fresh d95 process on the final binary checks that comparison:",
        f"{check['calls']} calls, GPU median {check['gpu_median_ms']:.3f} ms, "
        f"p95 {check['gpu_p95_ms']:.3f} ms, worst {check['worst_bits']:.4f} bits.",
    ])
if "same_final_build_selected_confirmation" in result:
    check = result["same_final_build_selected_confirmation"]
    lines.extend([
        "", "An adjacent fresh selected process on that same final binary:",
        f"{check['calls']} calls, GPU median {check['gpu_median_ms']:.3f} ms, "
        f"p95 {check['gpu_p95_ms']:.3f} ms, worst {check['worst_bits']:.4f} bits.",
        f"Adjacent final-build GPU latency change: {result['adjacent_final_build_gpu_change_percent']:+.2f}%.",
        "The earlier d95 median shifted by roughly 5% at final confirmation;",
        "use the adjacent final-build pair for the final latency comparison.",
    ])
lines.extend([
    "", "Some CUDA-event intervals exceed host-clock intervals; both are retained.",
    "No speedup is claimed: the selected route saves one level but has higher latency.",
])
(directory / "measurements.md").write_text("\n".join(lines) + "\n")
