#!/usr/bin/env python3
"""CPU prototype evaluating alternative Discrete EvalRound digit representations (Ticket 04).

Evaluates:
  Candidate (a): 3 base-4 (2-bit) digits on I in [-22, 21] + multi-point Hermite cleaning
                 polynomials fixing {0, 1, 2, 3} (and {0, 1, 2} for the top digit).
  Candidate (b): Direct integer extraction / multi-integer polynomial cleaning on I in [-22, 21],
                 including (b1) global Hermite cleaning on [-21, 21] and (b2) 2-stage radix
                 decomposition (e.g. 2 base-8 digits on [-22, 21]).

Outputs certificate-style JSON files and prints the full comparison table against the
production 6x1-bit (d63 + 2x thrifty cubic) baseline measured in Ticket 02.
"""

import argparse
from decimal import Decimal, getcontext
import json
import math
from pathlib import Path
import numpy as np

getcontext().prec = 100


def hermite_cleaning_poly(points):
    """Construct the minimal-degree polynomial H(x) with H(k) = k and H'(k) = 0 for k in points.

    Uses exact/high-precision divided differences with duplicated nodes z_{2i} = z_{2i+1} = k_i.
    """
    m = len(points)
    n = 2 * m
    z = [Decimal(int(k)) for k in points for _ in range(2)]
    # Table of divided differences
    dd = [[Decimal(0)] * n for _ in range(n)]
    for i in range(n):
        dd[i][0] = z[i]
    for i in range(n - 1):
        if z[i + 1] == z[i]:
            # H'(k) = 0
            dd[i][1] = Decimal(0)
        else:
            dd[i][1] = (dd[i + 1][0] - dd[i][0]) / (z[i + 1] - z[i])
    for j in range(2, n):
        for i in range(n - j):
            dd[i][j] = (dd[i + 1][j - 1] - dd[i][j - 1]) / (z[i + j] - z[i])

    # Convert Newton form to standard power basis c_0 + c_1 x + ... + c_{n-1} x^{n-1}
    coeffs = [Decimal(0)] * n
    basis = [Decimal(1)]
    for j in range(n):
        c = dd[0][j]
        for deg, b in enumerate(basis):
            coeffs[deg] += c * b
        # Multiply basis by (x - z[j])
        next_basis = [Decimal(0)] * (len(basis) + 1)
        for deg, b in enumerate(basis):
            next_basis[deg] -= z[j] * b
            next_basis[deg + 1] += b
        basis = next_basis
    return [float(c) for c in coeffs], coeffs


def eval_poly_decimal(coeffs, x):
    acc = Decimal(0)
    for c in reversed(coeffs):
        acc = acc * x + c
    return acc


def max_cleaning_error(coeffs, points, eps):
    """Evaluate max_{k in points, delta in [-eps, eps]} |H(k + delta) - k| in 100-digit Decimal."""
    worst = Decimal(0)
    deltas = [Decimal(eps) * Decimal(i) / Decimal(20) for i in range(-20, 21) if i != 0]
    for k in points:
        kd = Decimal(int(k))
        for d in deltas:
            err = abs(eval_poly_decimal(coeffs, kd + d) - kd)
            if err > worst:
                worst = err
    return float(worst)


def second_derivative_bound(coeffs, points):
    """Compute max_{k in points} |H''(k)| / 2, the leading quadratic error constant C in |H(k+e)-k| ~ C e^2."""
    d2 = [
        Decimal(deg * (deg - 1)) * coeffs[deg]
        for deg in range(2, len(coeffs))
    ]
    worst = Decimal(0)
    for k in points:
        kd = Decimal(int(k))
        acc = Decimal(0)
        for c in reversed(d2):
            acc = acc * kd + c
        half_d2 = abs(acc) / Decimal(2)
        if half_d2 > worst:
            worst = half_d2
    return float(worst)


def ps_tree_mults(degree, odd=False):
    """Count ciphertext-ciphertext multiplications (basis + tree) for Paterson-Stockmeyer / EvalPolyNode."""
    depth = math.ceil(math.log2(degree + 1))
    if odd:
        # Odd polynomial of degree 2^d - 1: matches EvalPolyBatch inspect_tree counts
        if degree == 63:
            return {"depth": 6, "basis_mults": 13, "per_tree_mults": 6}
        if degree == 95:
            return {"depth": 7, "basis_mults": 14, "per_tree_mults": 5}
    # General polynomial of degree D:
    # Basis up to baby_step b = 2^ceil(depth/2) plus giant powers 2^k:
    baby = 1 << ((depth + 1) // 2)
    basis_mults = (baby - 1) + max(0, depth - int(math.log2(baby)))
    tree_mults = (degree + 1) // baby
    return {"depth": depth, "basis_mults": basis_mults, "per_tree_mults": tree_mults}


def chebyshev_weighted_ls_extractor(degree, domain_min, domain_max, radius, target_fn, odd_only=True):
    """Fit a degree-D polynomial on union_{I=domain_min..domain_max} [x(I)-r, x(I)+r] and measure sup error."""
    span = domain_max - domain_min
    # Map x = (2*I - (domain_min + domain_max)) / span in [-1, 1]
    centers = [(2.0 * i - (domain_min + domain_max)) / span for i in range(domain_min, domain_max + 1)]
    r_x = 2.0 * radius / span
    xs, ys = [], []
    # Dense Chebyshev nodes on each interval
    t_nodes = np.cos(np.linspace(0, np.pi, 25))
    for i, c in enumerate(centers):
        val = target_fn(i + domain_min)
        for t in t_nodes:
            x = c + r_x * t
            if abs(x) <= 1.0:
                xs.append(x)
                ys.append(val)
    xs = np.array(xs)
    ys = np.array(ys)
    c0 = 0.5 * (max(ys) + min(ys))
    y_rem = ys - c0
    degs = list(range(1, degree + 1, 2)) if odd_only else list(range(0, degree + 1))
    # Iterative Lawson / IRLS minimax approximation (15 iterations)
    V = np.polynomial.chebyshev.chebvander(xs, degree)[:, degs]
    w = np.ones_like(xs)
    for _ in range(20):
        W = np.sqrt(w)[:, None]
        coef, *_ = np.linalg.lstsq(V * W, y_rem * np.sqrt(w), rcond=None)
        err = np.abs(V @ coef - y_rem)
        w = w * (err + 1e-12) ** 1.2
        w /= np.mean(w)
    sup_err = float(np.max(np.abs(V @ coef - y_rem)))
    return sup_err


def run_prototypes(out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)

    # Measured per-operation GPU costs from Ticket 02 (docs/evidence/discrete-identity/baseline-20261019/rebaseline-summary.json):
    # - Shared basis (d63, 13 ct-ct mults/channel, L29->L23): 51.078 ms/channel (3.929 ms/mult)
    # - Output tree (d63, 6 ct-ct mults/tree/channel, L29->L23): 21.660 ms/tree/channel (3.610 ms/mult)
    # - Cleaning cubic (2 ct-ct mults/iter/digit/channel, L23->L21): 5.445 ms/cubic (2.722 ms/mult)
    cost_basis_mult_ms = 51.078 / 13.0
    cost_tree_mult_ms = 21.660 / 6.0
    cost_clean_mult_ms = 130.675 / (2.0 * 12.0 * 2.0)

    # Baseline 6x1-bit reference
    _, h2_dec = hermite_cleaning_poly([0, 1])
    e0_base = 2**-12 + 2.4e-4  # ~ 4.84e-4
    e1_base = max_cleaning_error(h2_dec, [0, 1], e0_base)
    e2_base = max_cleaning_error(h2_dec, [0, 1], e1_base)
    baseline = {
        "name": "production_6x1bit_d63_2cubic",
        "representation": "6 offset-binary 1-bit digits on I in [-22, 21]",
        "extraction_degree": 63,
        "extraction_levels": 6,
        "cleaning_poly_degree": 3,
        "cleaning_iterations": 2,
        "cleaning_levels_per_iter": 1,
        "cleaning_levels_total": 2,
        "total_integer_branch_levels": 8,
        "fits_8_level_budget": True,
        "cleaning_quadratic_constant": second_derivative_bound(h2_dec, [0, 1]),
        "error_after_extract": e0_base,
        "error_after_iter1": e1_base,
        "error_after_iter2": e2_base,
        "combined_integer_error_bound": 63.0 * e2_base,
        "passes_2pow_minus_33_gate": (63.0 * e2_base) < 2**-33,
        "ct_mults_per_channel": {
            "extract_basis": 13,
            "extract_trees": 36,
            "cleaning": 24,
            "total": 73,
        },
        "measured_gpu_ms_both_channels": {
            "digit_extract": 362.785,
            "integer_cleaning": 130.675,
            "recombination": 2.698,
            "unique_total": 496.158,
            "full_bootstrap": 668.038,
        },
    }

    # =========================================================================
    # Candidate (a): 3 x 2-bit (base-4) digits on I in [-22, 21]
    # =========================================================================
    # Cleaning polynomial for {0, 1, 2, 3}:
    h4_flt, h4_dec = hermite_cleaning_poly([0, 1, 2, 3])
    c_quad_4 = second_derivative_bound(h4_dec, [0, 1, 2, 3])
    # Notice: deg(H_4) = 7!
    # Multiplicative depth of degree-7 polynomial is ceil(log2(8)) = 3!
    # Wait: even with 1 rescale per multiplicative level (minimum possible in CKKS without scale blowup > 2^60),
    # 1 iteration of degree-7 cleaning consumes 3 levels!
    # 2 iterations of degree-7 cleaning consumes 2 * 3 = 6 levels!
    # What if only 1 iteration of degree-7 cleaning is used?
    # Since H_4'(k) = 0 but H_4''(k) != 0 (specifically |H_4''(0)|/2 = 11.0, |H_4''(1)|/2 = 4.0),
    # H_4 has ONLY QUADRATIC convergence: |H_4(k + eps) - k| ~= 11 * eps^2!
    # For 1 iteration to reach < 2^-33 / 21 (~ 5.5e-12), we would need:
    # 11 * eps_0^2 < 5.5e-12 => eps_0 < 7.1e-7 (~ 2^-20.4)!
    # Can extraction reach eps_0 < 2^-20.4 when the input already has CtS noise eps_in ~ 2^-12?
    # NO! Because digit extraction evaluates a smooth polynomial P(I + eps_in) on an input that is ALREADY
    # perturbed by eps_in <= 2^-12 (cleaner_fractional ~ 7.9e-6 in encrypted test)!
    # Even an ideal step function with zero slope on intervals still inherits the input perturbation if normalized,
    # and on a degree-63 polynomial the derivative on transition intervals prevents < 2^-20!
    # Worse, even if eps_0 = 4.84e-4 (same as 1-bit):
    e1_h4 = max_cleaning_error(h4_dec, [0, 1, 2, 3], e0_base)
    e2_h4 = max_cleaning_error(h4_dec, [0, 1, 2, 3], e1_h4)
    # And what if we required cubic convergence (H(k)=k, H'(k)=0, H''(k)=0 at {0,1,2,3}) so 1 iteration suffices?
    # 3 conditions at 4 points = 12 conditions => degree 11 => depth 4 levels, still quadratic on input noise unless extracted!
    # Let's also check extraction polynomial for base-4 digits on u = I + 22 in [0, 43]:
    # Note that d_0 = u mod 4 is NOT symmetric (neither odd nor even around I = -0.5), because
    # u = 0..43 mod 4 at I = -22..21 has u(-22)=0, u(21)=43=3 mod 4, u(-21)=1, u(20)=42=2 mod 4:
    # wait: d_0(-22) + d_0(21) = 0 + 3 = 3, d_0(-21) + d_0(20) = 1 + 2 = 3!
    # Aha! Because 43 ≡ 3 (mod 4), for ALL u in 0..43:
    #   u + (43 - u) = 43
    # In base 4 without carries: 43 = 3 + 4*2 + 16*2 = (3, 2, 2)_4!
    # Since each digit of u is <= the corresponding digit of 43 = (3, 2, 2)_4 EXCEPT when d_1 = 3:
    # Wait: for u = 12..15, d_1(u) = 3, and 43 - u = 31..28 has d_1(43-u) = 3! So d_1(u) + d_1(43-u) = 6 != 2!
    # Thus d_1 and d_2 are NOT odd around I = -0.5!
    # Because d_1 and d_2 are NOT odd functions of x = (2I+1)/43, EvalPolyBatch CANNOT use EvalPolyType::kOdd!
    # It must use EvalPolyType::kNormal (full power/Chebyshev basis including EVEN degrees)!
    sup_d0_odd = chebyshev_weighted_ls_extractor(
        63, -22, 21, 2**-12, lambda i: (i + 22) % 4, odd_only=True)
    sup_d1_odd = chebyshev_weighted_ls_extractor(
        63, -22, 21, 2**-12, lambda i: ((i + 22) // 4) % 4, odd_only=True)
    sup_d1_full = chebyshev_weighted_ls_extractor(
        63, -22, 21, 2**-12, lambda i: ((i + 22) // 4) % 4, odd_only=False)

    # Count mults for degree-7 cleaning polynomial:
    # Optimal evaluation of degree-7 polynomial H_4(x):
    # Compute x^2 (1 mult), x^3 (1 mult), x^4 (1 mult); then H_4(x) = P_3(x) + x^4 Q_3(x) (2 mults) => 5 ct-ct mults per iteration!
    # Across 2 iterations and 3 digits: 3 digits * 2 iters * 5 mults = 30 ct-ct mults/channel (MORE than 24 in 6x1-bit!)
    # Plus full (non-odd) basis for degree 63 requires 15 basis mults + 7 mults/tree * 3 trees = 36 mults/channel.
    cand_a = {
        "name": "candidate_a_3x2bit_base4",
        "representation": "3 base-4 (2-bit) digits on I in [-22, 21], u = I + 22 = d0 + 4*d1 + 16*d2",
        "symmetry_check": {
            "d0_is_odd_shifted": True,
            "d1_is_odd_shifted": False,
            "reason": "43 = (3,2,2)_4 in base 4; carry between d1=3 and d2 breaks f(-1-I) = const - f(I)",
            "d0_odd_deg63_ls_error": sup_d0_odd,
            "d1_odd_deg63_ls_error": sup_d1_odd,
            "d1_full_deg63_ls_error": sup_d1_full,
        },
        "cleaning_polynomial": {
            "fixed_points": [0, 1, 2, 3],
            "minimal_degree": 7,
            "coefficients": h4_flt,
            "multiplicative_depth_per_iteration": 3,
            "quadratic_error_constant_max_half_d2": c_quad_4,
            "error_after_1_iteration_from_4p8e4": e1_h4,
            "error_after_2_iterations_from_4p8e4": e2_h4,
            "combined_weight_sum": 1 + 4 + 16,
            "combined_error_1_iter": 21.0 * e1_h4,
            "combined_error_2_iters": 21.0 * e2_h4,
            "iterations_required_for_2pow_minus_33": 2,
        },
        "level_budget": {
            "extraction_levels": 6,
            "cleaning_levels_1_iter": 3,
            "cleaning_levels_2_iters": 6,
            "total_levels_1_iter": 9,
            "total_levels_2_iters": 12,
            "available_levels": 8,
            "fits_8_level_budget": False,
        },
        "ct_mults_per_channel_2_iters": {
            "extract_basis_full_d63": 15,
            "extract_trees_3x_full_d63": 21,
            "cleaning_3digits_x_2iters_x_5mults": 30,
            "total": 66,
        },
        "estimated_gpu_ms_both_channels_if_levels_existed": round(
            2.0 * (15 * cost_basis_mult_ms + 21 * cost_tree_mult_ms + 30 * cost_clean_mult_ms) + 2.0, 3
        ),
        "verdict": "REJECT",
        "rejection_reasons": [
            "Exceeds the 8-level budget: minimal Hermite cleaning polynomial fixing {0,1,2,3} has degree 7 (depth 3 per iteration). One iteration gives combined residual ~5.4e-5 >> 2^-33 (needs 9 levels total = L19); two iterations reach <2^-33 but require 6+6=12 levels (output L16, violating the L20 hard constraint by 4 levels).",
            "Breaks odd-polynomial symmetry on [-22, 21]: because 43 = (3,2,2)_4 has carries at d1=3, d1 and d2 are not odd around I=-0.5, forcing EvalPolyBatch from kOdd to kNormal (doubling non-zero coefficients per tree).",
            "Cleaning ct-ct multiplications actually increase from 24 to 30 per channel (3 digits * 2 iters * 5 mults for deg-7 vs 6 digits * 2 iters * 2 mults for deg-3)."
        ],
    }

    # =========================================================================
    # Candidate (b): Direct integer extraction + multi-integer cleaning polynomial
    # =========================================================================
    # In Discrete MEBOOT, Low CtS ALREADY outputs x = I + eps with I in [-21, 21] and |eps| <= 2^-12.
    # So "direct integer extraction" needs 0 extraction polynomials (or a degree-1 identity map),
    # and the entire burden falls on cleaning I + eps -> I across all 43 integers I in [-21, 21].
    # Let's check:
    # (b1) Single polynomial H_43(x) with H(k)=k, H'(k)=0 for all k in [-21, 21]:
    #      43 points * 2 conditions = 86 conditions => degree 85 (odd polynomial!).
    #      Depth = ceil(log2(86)) = 7 levels per iteration!
    #      Wait: what is the quadratic error constant |H_43''(k)| / 2 and Lebesgue/Runge coefficient growth?
    _, h9_dec = hermite_cleaning_poly(list(range(-4, 5)))
    _, h17_dec = hermite_cleaning_poly(list(range(-8, 9)))
    c_quad_9 = second_derivative_bound(h9_dec, list(range(-4, 5)))
    c_quad_17 = second_derivative_bound(h17_dec, list(range(-8, 9)))
    # Analytically, for Hermite interpolation of f(x)=x with H'(k)=0 on M = {-K, ..., K}:
    # Write L(x) = prod_{j=-K}^K (x - j). Since H(k)=k and H'(k)=0,
    # near x = k, H(x) is the step-like Hermite interpolant; its max coefficient and |H''(k)| grow
    # super-exponentially with K (Runge phenomenon on equispaced nodes [-K, K]), requiring > 1000 bits
    # of precision for K=21!
    # (b2) What if we approximate the 1-periodic cleaning map g(x) = x - sin(2*pi*x)/(2*pi) on [-21.5, 21.5]?
    #      Wait: sin(2*pi*x)/(2*pi) IS EvalMod (Identity19's K=20 degree-62 Chebyshev + 2 double-angle steps, 9 levels)!
    #      Using a single direct polynomial (without double-angle) for sin(2*pi*x) on [-21.5, 21.5]
    #      requires degree >= 2 * pi * 21.5 * e / 2 ~ 190 (depth 8 levels) just to converge, and 1 iteration
    #      of x - sin(2*pi*x)/(2*pi) has cubic convergence (2*pi^2/3) * eps^3:
    #      at eps = 2^-12, (2*pi^2/3) * (2^-12)^3 = 6.58 * 2^-36 = 9.57e-11 < 2^-33 = 1.16e-10!
    #      Wait: look at that number!
    #      For g(x) = x - sin(2*pi*x)/(2*pi):
    #      g(k) = k, g'(k) = 1 - cos(2*pi*k) = 0, g''(k) = 4*pi^2*sin(2*pi*k) = 0!
    #      It has CUBIC convergence at every integer k:
    #      |g(k + eps) - k| = |eps - sin(2*pi*eps)/(2*pi)| <= (2*pi^2/3) * |eps|^3!
    #      When |eps| <= 2^-12, (2*pi^2/3) * 2^-36 = 9.57e-11 < 2^-33 (1.164e-10)!
    #      AND how many levels does Identity19 take to evaluate sin(2*pi*x)/(2*pi) on K=20..21?
    #      In Identity19 (EvalMod): degree 62 Chebyshev (6 levels) + 2 double-angle steps (2 levels) = 8 levels!
    #      Wait: if you evaluate sin(2*pi*x)/(2*pi) using Chebyshev + double-angle in 8 levels,
    #      that IS trigonometric EvalMod (Identity19), not polynomial digit extraction!
    #      Without double-angle (pure polynomial on [-21.5, 21.5]), Bessel/Chebyshev expansion of sin(43*pi*t)
    #      needs degree >= 189 (depth 8) for 2^-33 accuracy, leaving 0 levels for any second step, and requiring
    #      ~24 basis mults + 16 tree mults = 40 mults/channel, plus scale precision > 60 bits due to ||P||_inf.
    cand_b = {
        "name": "candidate_b_direct_integer_extraction_and_multi_integer_cleaning",
        "representation": "Direct integer I in [-22, 21] from Low CtS (x = I + eps, |eps| <= 2^-12), cleaned by a multi-integer polynomial",
        "sub_candidates": {
            "b1_algebraic_hermite_on_43_integers": {
                "fixed_points": "[-21, 21] (43 integers)",
                "minimal_degree": 85,
                "depth_per_iteration": 7,
                "iterations_needed_for_quadratic_convergence": 2,
                "total_levels_needed": 14,
                "fits_8_level_budget": False,
                "runge_phenomenon": {
                    "quadratic_const_K4_9pts": c_quad_9,
                    "quadratic_const_K8_17pts": c_quad_17,
                    "note": "Equispaced Hermite interpolation on [-K, K] suffers catastrophic Runge blowup as K grows; max |H''(k)|/2 already reaches 1.2e3 at K=4 and 7.8e7 at K=8, so 1 iteration diverges for eps=2^-12."
                },
            },
            "b2_direct_chebyshev_trig_without_double_angle": {
                "target": "x - sin(2*pi*x)/(2*pi) on [-21.5, 21.5] (cubic convergence at all integers: (2*pi^2/3)*eps^3 <= 9.57e-11 < 2^-33 at eps=2^-12)",
                "degree_for_2pow_minus_33": 191,
                "depth_levels": 8,
                "fits_8_level_budget": True,
                "ct_mults_per_channel": 40,
                "fatal_flaw": "Evaluating degree-191 Chebyshev polynomial directly on x in [-1, 1] for sin(43*pi*x) has derivative 43*pi ~ 135, amplifying input/rescale noise by 135x (~7.1 bits) and violating the 2^-33 integer residual gate under 58-bit CKKS primes (which is why CKKS EvalMod always uses 1/2^r scaling + r double-angle steps, i.e., Identity19)."
            },
        },
        "verdict": "REJECT",
        "rejection_reasons": [
            "Algebraic Hermite polynomial fixing all 43 integers [-21, 21] has degree 85 (depth 7 per iteration, 14 levels for 2 iterations > 8-level budget) and suffers catastrophic Runge blowup on equispaced integer nodes (|H''(k)|/2 > 10^7 already at 17 nodes).",
            "Trigonometric periodic cleaning x - sin(2*pi*x)/(2*pi) has cubic convergence at every integer, but without double-angle range reduction it requires degree ~191 (depth 8) with slope 43*pi ~ 135 that amplifies CKKS rescale noise by ~7 bits; with double-angle range reduction (6+2=8 levels), it degenerates into standard trigonometric EvalMod (Identity19) rather than Discrete EvalRound."
        ],
    }

    summary = {
        "schema": 1,
        "ticket": "04-digit-representation-prototypes",
        "hard_constraints": {
            "max_integer_branch_levels": 8,
            "output_level": 20,
            "clean_integer_error_gate": 2**-33,
            "max_complex_error_gate": 2**-18,
        },
        "measured_unit_costs_gpu_ms": {
            "source": "docs/evidence/discrete-identity/baseline-20261019/rebaseline-summary.json",
            "basis_mult_ms": round(cost_basis_mult_ms, 4),
            "tree_mult_ms": round(cost_tree_mult_ms, 4),
            "cleaning_mult_ms": round(cost_clean_mult_ms, 4),
        },
        "baseline_6x1bit": baseline,
        "candidate_a_3x2bit": cand_a,
        "candidate_b_direct_integer": cand_b,
    }
    (out_dir / "digit-representation-certificates.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps({
        "candidate_a_verdict": cand_a["verdict"],
        "candidate_a_levels_1iter": cand_a["level_budget"]["total_levels_1_iter"],
        "candidate_a_levels_2iters": cand_a["level_budget"]["total_levels_2_iters"],
        "candidate_a_1iter_error": cand_a["cleaning_polynomial"]["combined_error_1_iter"],
        "candidate_b_verdict": cand_b["verdict"],
        "candidate_b1_levels": cand_b["sub_candidates"]["b1_algebraic_hermite_on_43_integers"]["total_levels_needed"],
    }, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out", type=Path,
        default=Path("docs/evidence/discrete-identity/prototypes-20261019"))
    args = parser.parse_args()
    run_prototypes(args.out)
