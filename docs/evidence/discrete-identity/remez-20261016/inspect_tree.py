"""Static operation counts for the frozen EvalPolyNode decomposition.

These counts explain structure, not GPU latency. Run from the repository root.
"""

import hashlib
import json
import math
from pathlib import Path


def trim(values):
    while values and values[-1] == 0:
        values.pop()
    return values


def inspect(coefficients, margin, baby, required):
    degree = len(coefficients) - 1
    if degree < baby and math.ceil(math.log2(degree)) + 1 <= margin:
        required.update(i for i, value in enumerate(coefficients) if value)
        return 0, 1
    split = 1 << (degree.bit_length() - 1)
    required.add(split)
    low, high = coefficients[:split], coefficients[split:]
    for i in range(1, len(high)):
        low[split - i] -= high[i]
        high[i] *= 2
    trim(low)
    trim(high)
    products, leaves = 0, 0
    if len(low) > 1:
        count, leaf_count = inspect(low, margin, baby, required)
        products += count
        leaves += leaf_count
    if len(high) > 1:
        count, leaf_count = inspect(high, margin - 1, baby, required)
        products += count + 1
        leaves += leaf_count
    return products, leaves


def expand_basis(degree, basis):
    if degree <= 1 or degree in basis:
        return
    if degree & (degree - 1) == 0:
        left = degree // 2
    else:
        candidate = (1 << (degree.bit_length() - 1)) - 1
        left = max(candidate, degree - candidate)
    right = degree - left
    for child in (left, right, left - right):
        expand_basis(child, basis)
    basis.add(degree)


result = {"method": "static mirror of EvalPolyNode constructor and odd BasisMap closure",
          "evalpoly_source_sha256": hashlib.sha256(
              Path("src/extension/EvalPoly.cpp").read_bytes()).hexdigest(), "cases": []}
for filename, babies in (("discrete_direct6_d95.json", [16]),
                         ("discrete_direct6_k21_remez_d63.json", [8, 16, 32])):
    manifest = json.loads((Path("parameters/research") / filename).read_bytes())
    for baby in babies:
        required = set()
        rows = []
        for row in manifest["coefficients"]:
            coefficients = [float(value) for value in row]
            coefficients[0] = 0
            products, leaves = inspect(
                coefficients, math.ceil(math.log2(len(row))), baby, required)
            rows.append({"ct_ct_products": products, "leaves": leaves})
        basis = set()
        for degree in required:
            expand_basis(degree, basis)
        result["cases"].append({
            "manifest": filename, "baby_step": baby, "per_bit": rows,
            "shared_basis_ct_ct_products": len(basis),
            "total_ct_ct_products_per_channel": len(basis) + sum(
                row["ct_ct_products"] for row in rows),
        })
print(json.dumps(result, indent=2))
