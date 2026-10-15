"""Generate a sampled minimax direct extractor in the Chebyshev basis.

This implements the piecewise approximation option in CCS 2025 Sec. 3.2.
It is NOT the authors' unpublished coefficient table or a continuum Remez
certificate. The fixed constraints and independent validation are recorded.
"""

import argparse
import json
from pathlib import Path

import numpy as np
import scipy
from numpy.polynomial.chebyshev import chebvander
from scipy.optimize import linprog


def generate(width, degree, radius):
    divisor = 1 << (width - 1)
    odd = np.arange(1, degree + 1, 2)
    # Reflection about I=-1/2 complements every offset-binary digit.
    integers = np.repeat(np.arange(divisor), 17)
    noise = np.tile(np.linspace(-radius, radius, 17), divisor)
    arguments = (integers + 0.5 + noise) / divisor
    matrix = chebvander(arguments, degree)[:, odd]
    constraints = np.vstack(
        [np.column_stack([matrix, -np.ones(len(matrix))]),
         np.column_stack([-matrix, -np.ones(len(matrix))])]
    )
    coefficients = []
    optimizer_errors = []
    for bit in range(width):
        targets = (((integers + divisor) >> bit) & 1) - 0.5
        objective = np.zeros(len(odd) + 1)
        objective[-1] = 1
        result = linprog(
            objective, A_ub=constraints, b_ub=np.r_[targets, -targets],
            # A finite coefficient bound prevents HiGHS treating the nearly
            # confluent interval constraints as an unbounded numerical model.
            bounds=[(-1024, 1024)] * len(odd) + [(0, None)],
            method="highs",
            options={"dual_feasibility_tolerance": 1e-9, "primal_feasibility_tolerance": 1e-9},
        )
        if not result.success:
            raise RuntimeError(result.message)
        vector = np.zeros(degree + 1)
        vector[0], vector[odd] = 0.5, result.x[:-1]
        coefficients.append([format(float(value), ".17g") for value in vector])
        optimizer_errors.append(float(result.fun))
    return {
        "schema": 1,
        "provenance": "independent sampled minimax implementation of CCS2025 Sec3.2",
        "author_coefficients": False,
        "width": width, "degree": degree, "argument_divisor": divisor,
        "argument_offset": "0.5", "signed_convention": "offset binary: unsigned=I+2^(width-1)",
        "input_error": format(radius, ".17g"),
        "coefficient_basis": "Chebyshev",
        "generator": {"numpy": np.__version__, "scipy": scipy.__version__,
                      "points_per_cell": 17, "coefficient_bound": 1024},
        "optimizer_sampled_errors": optimizer_errors,
        "coefficients": coefficients,
        "continuum_bound_proved": False,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--width", type=int, choices=(5, 6), default=5)
    parser.add_argument("--degree", type=int, default=47)
    parser.add_argument("--log-input-error", type=int, default=-12)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.degree < (1 << args.width) - 1 or args.degree % 2 == 0:
        parser.error("odd degree covering all signed cells required")
    result = generate(args.width, args.degree, 2.0**args.log_input_error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ("width", "degree", "optimizer_sampled_errors")}))
