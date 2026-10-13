#!/usr/bin/env python3
"""Diagnose the bounded CRT interface using independent golden operator norms.

Requires NumPy. This floating-point check includes coefficient rounding and
an explicit numerical allowance; it is not a certified interval proof.
"""
import gzip
import json
import math
import struct
from pathlib import Path
import numpy as np

root = Path(__file__).resolve().parents[2]
with gzip.open(root / "parameters/meboot_golden_4434.bin.gz", "rb") as stream:
    assert stream.read(8) == b"MEBCTSG1"
    slots, count = struct.unpack("<II", stream.read(8))
    matrices = []
    for stage in range(2):
        diagonals, = struct.unpack("<I", stream.read(4))
        matrix = {}
        for _ in range(diagonals):
            offset, = struct.unpack("<I", stream.read(4))
            matrix[offset] = np.frombuffer(stream.read(slots * 16), "<c16").copy()
        matrices.append(matrix)

powers = np.array([pow(5, j, slots * 4) for j in range(slots)], dtype=np.int64)
indices = np.arange(slots, dtype=np.int64)
reverse = np.zeros(slots, dtype=np.int64)
for bit in range(int(math.log2(slots))):
    reverse |= ((indices >> bit) & 1) << (int(math.log2(slots)) - 1 - bit)


def polynomial(values):
    data = values.copy()
    stride = slots // 2
    while stride:
        blocks = data.reshape(-1, stride * 2)
        left, right = blocks[:, :stride].copy(), blocks[:, stride:].copy()
        twiddle = np.exp(-2j * np.pi * (powers[:stride] % (8 * stride)) / (8 * stride))
        blocks[:, :stride] = left + right
        blocks[:, stride:] = (left - right) * twiddle
        stride //= 2
    return data[reverse] / slots


def norm(matrix):
    return sum(np.abs(p.real).sum() + np.abs(p.imag).sum()
               for p in map(polynomial, matrix.values()))


combined = {}
for r2, d2 in matrices[1].items():
    for r1, d1 in matrices[0].items():
        rotation = (r1 + r2) % slots
        if rotation not in combined:
            combined[rotation] = np.zeros(slots, dtype=np.complex128)
        combined[rotation] += d2 * np.roll(d1, -r2)
norm1, norm2, norm21 = norm(matrices[0]), norm(matrices[1]), norm(combined)
records = []
for profile, bits in (("bootparam_40.json", 32), ("bootparam_40_64bit.json", 64)):
    cfg = json.loads((root / "parameters" / profile).read_text())
    level = cfg["max_key_switch_level"] - 2
    bases = [math.prod(cfg["main_primes"][:nm]) * math.prod(cfg["terminal_primes"][:nt])
             for nm, nt in cfg["level_config"]]
    drops = [None] + [bases[i] / bases[i - 1] for i in range(1, len(bases))]
    start_bits = round(math.log2(drops[level])) / 2**11 + sum(
        math.log2(drops[level - i]) / 2**(i + 1) for i in range(11))
    nm0, nt0 = cfg["level_config"][0]
    q0 = math.prod(cfg["main_primes"][:nm0] + cfg["terminal_primes"][:nt0])
    ql = cfg["main_primes"][-1]
    factor = 2**start_bits / (2**cfg["log_degree"] * q0 * 64)
    gain1 = factor**.25 / 64 if bits == 32 else factor**.25
    gain2 = 2**max(16, 46 - math.floor(math.log2(ql))) if bits == 32 else factor**.25
    q_prefix = bases[-1] / bases[cfg["max_key_switch_level"]]
    degree = 2**cfg["log_degree"]
    assert degree == 2 * slots, "golden fixture and profile ring degrees differ"
    numerical_allowance = 1e-12
    delta1 = len(matrices[0]) * degree * (
        numerical_allowance * gain1 + 1 / q_prefix)
    delta2 = len(matrices[1]) * degree * (
        numerical_allowance * gain2 + 1 / ql)
    a1 = norm1 * gain1 + delta1
    a2 = norm2 * gain2 + delta2
    a21 = (norm21 * gain1 * gain2 + norm1 * gain1 * delta2 +
           norm2 * gain2 * delta1 + delta1 * delta2 +
           len(combined) * degree * numerical_allowance * gain1 * gain2)
    nm, nt = cfg["level_config"][cfg["max_key_switch_level"]]
    first_q = list(reversed(cfg["terminal_primes"][:nt])) + cfg["main_primes"][:nm]
    crt = math.prod(first_q[:3 if bits == 32 else 2])
    # Approximate RNS rescale can differ from the ideal quotient by half the
    # number of removed limbs; the LCR quotient removes one centered remainder.
    prefix_limb_count = cfg["level_config"][-1][1] - nt
    prefix_rounding = prefix_limb_count / 2
    b0 = (q0 - 1) // 2
    b1 = a1 * b0 + prefix_rounding
    representative = a21 * b0 + a2 * prefix_rounding + .5
    log_q = sum(map(math.log2, first_q))
    h_plus_one = cfg["sparse_hamming_weight"] + 1
    phase1_margin = log_q - 1 - math.log2(a1 * h_plus_one * b0)
    phase2_margin = log_q - 1 - math.log2(a2 * ql * h_plus_one * b1)
    record = dict(
        bits=bits, unit_norm1=norm1, unit_norm2=norm2, unit_norm21=norm21,
        gain1=gain1, gain2=gain2, a1_bound=a1, a2_bound=a2, a21_bound=a21,
        representative_bound_log2=math.log2(representative),
        crt_half_log2=math.log2(crt / 2),
        c1_bound_log2=math.log2(b1),
        phase1_margin_bits=phase1_margin, phase2_margin_bits=phase2_margin,
        conservative_representative_pass=bool(representative < crt / 2),
        conservative_c1_pass=bool(b1 < crt / 2),
        numerical_allowance_per_unit_diagonal=numerical_allowance,
        caveat="conservative floating-point diagnostic, not a certified interval proof",
    )
    assert record["conservative_representative_pass"] and record["conservative_c1_pass"]
    assert min(phase1_margin, phase2_margin) > 0
    records.append(record)
print(json.dumps(records, indent=2))
