#!/usr/bin/env python3
"""Attribute diagnostic refresh errors using decoded stages, outside timing."""
import argparse
import cmath
import json
import math
from pathlib import Path


def cts(values):
    out = list(values)
    slots = len(out)
    stride = slots // 2
    while stride:
        modulus = stride * 8
        for start in range(0, slots, 2 * stride):
            factor = 1
            for j in range(stride):
                twiddle = cmath.exp(-2j * math.pi * factor / modulus)
                x, y = out[start + j], out[start + j + stride]
                out[start + j] = x + y
                out[start + j + stride] = (x - y) * twiddle
                factor = factor * 5 % modulus
        stride //= 2
    return out


def maximum_error(actual, expected):
    errors = [abs(a - b) for a, b in zip(actual, expected)]
    slot = max(range(len(errors)), key=errors.__getitem__)
    return {"maximum_complex_error": errors[slot], "slot": slot,
            "actual": [actual[slot].real, actual[slot].imag],
            "expected": [expected[slot].real, expected[slot].imag]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("stages", type=Path)
    parser.add_argument("--route", choices=["ordinary", "joint4434"], required=True)
    parser.add_argument("--profile", type=Path,
                        help="Explicit native profile; default is the pinned reference")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    profile = (json.loads(args.profile.read_text()) if args.profile else
               json.loads((root / ".scratch/identity16-18/reference-selected.json").read_text())[args.route])
    binding = profile.get("native_scaling_binding", {})
    records = json.loads(args.stages.read_text())
    stages = {}
    for record in records:
        stages.setdefault(record["stage"], []).append([complex(*v) for v in record["values"]])
    count = len(stages["output"][0])
    inputs = stages.get("input", [[
        complex((i % 33 / 32 - .5) / 2, ((i // 33) % 33 / 32 - .5) / 2)
        for i in range(count)]])[0]
    report = {}
    for name in ["stc", "entry"]:
        recovered = [v * 64 / count for v in cts(stages[name][0])]
        report[name + "_inverse_error"] = maximum_error(recovered, inputs)
    gain = 1 / (profile["mod1"]["K"] * profile["mod1_q_diff"])
    gain *= binding.get("proper_cts_gain_multiplier", 1)
    ideal_cts = [v * gain / (2 * count) for v in cts(stages["dense"][0])]
    report["proper_cts_arithmetic_error"] = maximum_error(stages["cts"][0], ideal_cts)
    ideal_bypass = [v * .25 / (2 * count) for v in cts(stages["dense"][0])]
    report["bypass_cts_arithmetic_error"] = maximum_error(stages["bypass-cts"][0], ideal_bypass)
    nonlinear = stages["mod1"]
    pre = stages["pre-mod1"]
    # Recompute the final combine in double precision, preserving each observed
    # channel. This also exposes cancellation of their large integer parts.
    bypass = stages["bypass-cts"][0]
    correction_gain = profile["mod1"]["K"] * profile["mod1_q_diff"] * .25
    correction_gain *= binding.get("mod1_return_gain_multiplier", 1)
    expected = [
        256 * (nonlinear[0][i] + 1j * nonlinear[1][i] -
               correction_gain * (pre[0][i] + 1j * pre[1][i]) + 2 * bypass[i])
        for i in range(count)
    ]
    report["finish_arithmetic_error"] = maximum_error(stages["output"][0], expected)
    coefficients = [v[0] for v in profile["mod1_coefficients_float64"]]
    k = profile["mod1"]["K"]
    def plain_mod1(value):
        # Keep the decoded imaginary residual: the correction cancels the
        # polynomial's local complex gain as well as its real gain.
        x = value / 1024 - .25 / k
        t0, t1 = 1.0, x
        result = coefficients[0] + coefficients[1] * x
        for coefficient in coefficients[2:]:
            t0, t1 = t1, 2 * x * t1 - t0
            result += coefficient * t1
        root = profile["mod1_sqrt2pi"]
        for _ in range(3):
            root *= root
            result = 2 * result * result - root
        return 1024 * result
    ideal_nonlinear = [[plain_mod1(v) for v in channel] for channel in pre]
    ideal_finish = [
        256 * (ideal_nonlinear[0][i] + 1j * ideal_nonlinear[1][i] -
               correction_gain * (pre[0][i] + 1j * pre[1][i]) + 2 * bypass[i])
        for i in range(count)
    ]
    report["mod1_arithmetic_output_error"] = maximum_error(expected, ideal_finish)
    report["ideal_mod1_pipeline_output_error"] = maximum_error(ideal_finish, inputs)
    report["output_error"] = maximum_error(stages["output"][0], inputs)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
