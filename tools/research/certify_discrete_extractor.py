"""Exact-rational continuum bounds for independent signed digit extractors.

This checks the real polynomial defined by the manifest's binary64
coefficients. It does not bound CKKS evaluation, encoding, or key-switch error.
"""

import argparse
import hashlib
import json
import math
from decimal import Decimal, ROUND_CEILING, localcontext
from fractions import Fraction
from pathlib import Path


CONTRACT = Fraction(1, 512)


def exact_binary64(value):
    if not isinstance(value, str):
        raise ValueError("coefficient must be a decimal string")
    number = float(value)
    if not math.isfinite(number):
        raise ValueError("coefficient must be finite binary64")
    return Fraction.from_float(number)


def ratio(value):
    return f"0x{value.numerator:x}/0x{value.denominator:x}"


def upper_decimal(value):
    with localcontext() as context:
        context.prec = 24
        context.rounding = ROUND_CEILING
        return str(Decimal(value.numerator) / Decimal(value.denominator))


def chebyshev_taylor(center, degree):
    """Return exact Taylor vectors for T_n(center + t), 0 <= n <= degree."""
    previous = [Fraction(1)]
    result = [previous]
    if degree == 0:
        return result
    current = [center, Fraction(1)]
    result.append(current)
    for n in range(2, degree + 1):
        following = [Fraction(0)] * (n + 1)
        for k, term in enumerate(current):
            following[k] += 2 * center * term
            following[k + 1] += 2 * term
        for k, term in enumerate(previous):
            following[k] -= term
        result.append(following)
        previous, current = current, following
    return result


def polynomial_taylor(coefficients, basis, degree):
    result = [Fraction(0)] * (degree + 1)
    for scalar, terms in zip(coefficients, basis):
        if scalar:
            for k, term in enumerate(terms):
                result[k] += scalar * term
    return result


def cell_bound(taylor, digit, radius):
    powers = [Fraction(1)]
    for _ in range(1, len(taylor)):
        powers.append(powers[-1] * radius)
    return abs(taylor[0] - digit) + sum(
        (abs(taylor[k]) * powers[k] for k in range(1, len(taylor))),
        Fraction(0),
    )


def clean_bound(error):
    return (3 + 2 * error) * error * error


def reject_json_constant(value):
    raise ValueError(f"invalid JSON constant {value}")


def certify(raw):
    manifest = json.loads(raw, parse_constant=reject_json_constant)
    width, degree = manifest.get("width"), manifest.get("degree")
    if type(width) is not int or width not in (5, 6):
        raise ValueError("expected five-bit or six-bit manifest")
    if type(degree) is not int or degree < 1 or degree % 2 == 0:
        raise ValueError("expected positive odd degree")
    divisor = 1 << (width - 1)
    integer_domain = manifest.get("integer_domain", [-divisor, divisor - 1])
    if (not isinstance(integer_domain, list) or len(integer_domain) != 2 or
            any(type(value) is not int for value in integer_domain) or
            not -divisor <= integer_domain[0] <= 0 or
            not 0 <= integer_domain[1] < divisor or
            (integer_domain[0] != -divisor and
             integer_domain[0] not in (-integer_domain[1], -integer_domain[1] - 1))):
        raise ValueError("invalid signed integer domain")
    lower_integer, upper_integer = integer_domain
    if manifest.get("coefficient_basis") != "Chebyshev":
        raise ValueError("unexpected coefficient basis")
    argument_map = manifest.get("argument_map")
    if argument_map is None:
        if manifest.get("argument_divisor") != divisor:
            raise ValueError("unexpected argument divisor")
        argument_gain = Fraction(1, divisor)
    else:
        denominator = argument_map["center_divisor"]
        if (type(denominator) is not int or denominator <= 0 or
                argument_map["center_numerator"] != "(2*I+1)"):
            raise ValueError("unsupported argument map")
        argument_gain = Fraction(2, denominator)
        if (Fraction(argument_map["error_scale"]) != argument_gain or
                Fraction(argument_map["encoded_affine_value"]) != argument_gain or
                Fraction(argument_map["encoded_affine_integer"],
                         argument_map["encoded_affine_scale"]) != argument_gain):
            raise ValueError("inconsistent affine encoding")
    if manifest.get("signed_convention") != "offset binary: unsigned=I+2^(width-1)":
        raise ValueError("unexpected signed convention")
    if not isinstance(manifest["argument_offset"], str) or Fraction(
            manifest["argument_offset"]) != Fraction(1, 2):
        raise ValueError("expected argument offset 1/2")
    if not isinstance(manifest["input_error"], str):
        raise ValueError("input error must be an exact decimal string")
    input_error = Fraction(manifest["input_error"])
    if input_error != Fraction(1, 4096):
        raise ValueError("expected input error 2^-12")
    if manifest.get("author_coefficients") is not False:
        raise ValueError("manifest must identify coefficients as independently generated")
    rows = manifest["coefficients"]
    if len(rows) != width or any(len(row) != degree + 1 for row in rows):
        raise ValueError("expected complete coefficient rows matching width and degree")
    coefficients = [[exact_binary64(value) for value in row] for row in rows]
    if any(row[0] != Fraction(1, 2) or any(row[2::2]) for row in coefficients):
        raise ValueError("expected half-offset polynomials with odd remainders")
    radius = input_error * argument_gain
    cells = []
    maxima = [Fraction(0)] * width
    worst_centers = [None] * width
    for integer in range(lower_integer, upper_integer + 1):
        center = (Fraction(integer) + Fraction(1, 2)) * argument_gain
        basis = chebyshev_taylor(center, degree)
        unsigned = integer + divisor
        bounds = []
        for bit, row in enumerate(coefficients):
            digit = (unsigned >> bit) & 1
            taylor = polynomial_taylor(row, basis, degree)
            bound = cell_bound(taylor, digit, radius)
            bounds.append({"bit": bit, "digit": digit,
                           "bound_rational": ratio(bound),
                           "bound_decimal_upper": upper_decimal(bound)})
            if bound > maxima[bit]:
                maxima[bit], worst_centers[bit] = bound, integer
        cells.append({"integer": integer, "bounds": bounds})
    extraction = maxima.copy()
    cleaned = {}
    for iteration in range(1, 4):
        maxima = [clean_bound(error) for error in maxima]
        if iteration in (2, 3):
            combined = sum((1 << bit) * error for bit, error in enumerate(maxima))
            cleaned[str(iteration)] = {
                "per_bit_rational": [ratio(error) for error in maxima],
                "per_bit_decimal_upper": [upper_decimal(error) for error in maxima],
                "combined_signed_integer_error_rational": ratio(combined),
                "combined_signed_integer_error_decimal_upper": upper_decimal(combined),
            }
    passed = all(value <= CONTRACT for value in extraction)
    return {
        "schema": 1,
        "width": width,
        "degree": degree,
        "method": "exact binary64 coefficients; exact rational Chebyshev Taylor recurrence; triangle bound on each full cell",
        "rational_format": "0xNUMERATOR/0xDENOMINATOR, both hexadecimal integers",
        "manifest_sha256": hashlib.sha256(raw).hexdigest(),
        "coefficient_interpretation": "Fraction.from_float(float(decimal_string)); no float used in bound inequalities",
        "input_error_rational": ratio(input_error),
        "argument_radius_rational": ratio(radius),
        "domain": (
            f"signed centers {lower_integer}..{upper_integer}, "
            "each with input error +/-2^-12"
        ),
        "digit_contract_rational": ratio(CONTRACT),
        "cell_bounds": cells,
        "extraction": [{"bit": bit, "worst_integer": worst_centers[bit],
                        "bound_rational": ratio(value),
                        "bound_decimal_upper": upper_decimal(value),
                        "passes_contract": value <= CONTRACT}
                       for bit, value in enumerate(extraction)],
        "cleaning_recurrence": "E_next = (3 + 2 E) E^2; exact polynomial arithmetic only",
        "signed_recombination": f"I = sum(bit_j * 2^j for j=0..{width - 1}) - {divisor}; offset -{divisor} is exact",
        "combined_error_rule": f"sum(2^j * E_j for j=0..{width - 1})",
        "cleaned": cleaned,
        "continuum_bound_proved": True,
        "extraction_contract_passed": passed,
        "encrypted_evaluation_error_included": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path(
        "parameters/research/discrete_direct5_d47.json"))
    parser.add_argument("--output", type=Path, default=Path(
        "docs/evidence/discrete-identity/direct5-certificate.json"))
    args = parser.parse_args()
    certificate = certify(args.manifest.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(certificate, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"extraction_contract_passed": certificate["extraction_contract_passed"],
                      "worst_decimal_upper": [row["bound_decimal_upper"]
                                              for row in certificate["extraction"]],
                      "output": str(args.output)}))
    if not certificate["extraction_contract_passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
