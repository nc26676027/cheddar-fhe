#!/usr/bin/env python3
"""Independent conservative selected-chain integer bound.

This is a qualification envelope, not a security proof. Native preparation
must still verify the encoded diagonal norms and the shared GPU CRT boundary.
"""
import json
from decimal import Decimal, getcontext
from pathlib import Path

getcontext().prec = 90
ROOT = Path(__file__).resolve().parents[2]
profile = json.loads((ROOT / "parameters/identity16_joint4434_selected.json").read_text())
q0, q1 = map(Decimal, profile["q"][:2])
q_extra = Decimal(profile["proper_extra_q"])
q_l = Decimal(profile["q"][-1])
n = Decimal(65536)
# The merged width-3 and width-4 butterfly diagonals have disjoint row
# paths and magnitude at most one before distributing the common gain.
binding = profile.get("native_scaling_binding", {})
gain = (Decimal(binding.get("proper_cts_gain_multiplier", 1)) /
        (Decimal(27) * Decimal(str(profile["mod1_q_diff"])) * n)).sqrt().sqrt()
eps = Decimal(2) ** -52
# Cover complex products, twiddle evaluation and the 15 encoding butterflies.
# This numerical guard needs confirmation against the native encoded matrix
# oracle; it is kept explicit instead of treating random passes as proof.
gamma = Decimal(1024) * eps / (1 - Decimal(1024) * eps)
matrix_l1 = lambda scale: n.sqrt() * gain * scale * (1 + gamma) + n
entry_scale = Decimal(2) ** Decimal(str(profile["stc_output_scale_log2_override"]))
scale_down_integer = max(1, int((q0 / Decimal(1024) / entry_scale).to_integral_value()))
entry_scale *= scale_down_integer
scale_up_integer = int(((Decimal(2) ** 48) / entry_scale).to_integral_value())
lift_bound = q0 / 2 * scale_up_integer
m1 = 8 * lift_bound * matrix_l1(q_extra) / q_extra + 1
m2 = 31 * m1 * matrix_l1(q_l) / q_l + 1
half_crt = (int(q0) * int(q1)) // 2
report = {
    "reference_revision": "6330f64b178b6215edd35baf5b11c8da558ca95c",
    "method": "componentwise centered Q0 bound; convolution l1 Cauchy-Schwarz; truncation guard N; centered rescale guard 1",
    "rounding_note": "Native BigInt(double) truncates, so use N rather than N/2 for the aggregate encoding guard.",
    "scale_down_integer": scale_down_integer,
    "scale_up_integer": scale_up_integer,
    "m1_component_bound": str(m1),
    "m2_plain_quotient_bound": str(m2),
    "centered_two_limb_capacity": str(half_crt),
    "margin_capacity_over_m2": str(Decimal(half_crt) / m2),
    "eligible_under_stated_matrix_bound": m1 < half_crt and m2 < half_crt,
    "qualification": "runtime-eligible only when this envelope and native encoded-diagonal norm checks pass; security proof out of scope",
    "boundary_cases": [str(x) for x in (0, 1, -1, half_crt, half_crt - 1, -half_crt, -half_crt + 1)],
}
print(json.dumps(report, indent=2))
