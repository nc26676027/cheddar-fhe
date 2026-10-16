"""Independent high-precision mathematics; never used by GPU evaluation.

Source: Choe et al., CCS 2025, Secs. 3.1-3.2, Fig. 2, Sec. 5.2.
The offset-binary convention is an explicit implementation choice.
"""

import argparse
import hashlib
import json
import unittest
from pathlib import Path

import mpmath as mp

mp.mp.dps = 100


def digits(integer, width):
    offset = 1 << (width - 1)
    if not -offset <= integer < offset:
        raise ValueError("integer outside the declared signed domain")
    unsigned = integer + offset
    return [(unsigned >> bit) & 1 for bit in range(width)]


def combine(values):
    return sum(mp.mpf(value) * (1 << bit) for bit, value in enumerate(values)) - (
        1 << (len(values) - 1)
    )


def h1(value):
    value = mp.mpf(value)
    return value * value * (3 - 2 * value)


def clean(value, iterations):
    for _ in range(iterations):
        value = h1(value)
    return value


def chebyshev(coefficients, value):
    """Clenshaw recurrence, independent of Cheddar's evaluation tree."""
    value = mp.mpf(value)
    next_one = next_two = mp.mpf(0)
    for coefficient in reversed(coefficients[1:]):
        current = mp.mpf(coefficient) + 2 * value * next_one - next_two
        next_two, next_one = next_one, current
    return mp.mpf(coefficients[0]) + value * next_one - next_two


def extract(value, manifest):
    argument = mp.mpf(value) + mp.mpf("0.5")
    if "argument_map" in manifest:
        argument *= mp.mpf(2) / manifest["argument_map"]["center_divisor"]
    else:
        argument /= manifest["argument_divisor"]
    return [chebyshev(coefficients, argument) for coefficients in manifest["coefficients"]]


def indirect_exact(integer, width):
    """DFT interpolation oracle for roots-to-bits, Sec. 3.2.

    The O(4^width) reference deliberately does not share the direct circuit.
    """
    order = 1 << width
    unsigned = integer + order // 2
    root = mp.exp(2j * mp.pi * unsigned / order)
    values = []
    for bit in range(width):
        coefficients = [
            sum(
                ((index >> bit) & 1) * mp.exp(-2j * mp.pi * power * index / order)
                for index in range(order)
            ) / order
            for power in range(order)
        ]
        values.append(sum(coefficient * root**power for power, coefficient in enumerate(coefficients)))
    return values


def validate(manifest):
    width = manifest["width"]
    offset = 1 << (width - 1)
    radius = mp.mpf(manifest["input_error"])
    maximum = mp.mpf(0)
    cleaned = {iteration: mp.mpf(0) for iteration in (2, 3)}
    worst = None
    # These are numerical validation points, not a continuum error proof.
    lower, upper = manifest.get("integer_domain", [-offset, offset - 1])
    for integer in range(lower, upper + 1):
        expected = digits(integer, width)
        for index in range(65):
            noise = radius * (2 * mp.mpf(index) / 64 - 1)
            observed = extract(integer + noise, manifest)
            error = max(abs(value - truth) for value, truth in zip(observed, expected))
            if error > maximum:
                maximum, worst = error, [integer, str(noise)]
            for iterations in cleaned:
                result = combine([clean(value, iterations) for value in observed])
                cleaned[iterations] = max(cleaned[iterations], abs(result - integer))
    return {
        "independent_reference": "100-decimal-digit mpmath Clenshaw",
        "width": width,
        "degree": manifest["degree"],
        "input_error": manifest["input_error"],
        "sample_count": (upper - lower + 1) * 65,
        "sampled_digit_error": str(maximum),
        "worst_case": worst,
        "sampled_clean_integer_error": {str(k): str(v) for k, v in cleaned.items()},
        "continuum_bound_proved": False,
        "encrypted_reproduction": False,
    }


def known_answers(manifest, manifest_name):
    width = manifest["width"]
    offset = 1 << (width - 1)
    radius = mp.mpf(manifest["input_error"])
    rows = []
    lower, upper = manifest.get("integer_domain", [-offset, offset - 1])
    for integer in range(lower, upper + 1):
        for noise in (-radius, mp.mpf(0), radius):
            observed = extract(integer + noise, manifest)
            rows.append({
                "input": str(integer + noise), "integer": integer,
                "digits": digits(integer, width),
                "extract": [str(value) for value in observed],
                "clean": [[str(clean(value, iteration)) for value in observed]
                          for iteration in (1, 2)],
            })
    return {"reference": "independent mpmath 100-digit Clenshaw",
            "manifest": manifest_name, "rows": rows}


class ReferenceTest(unittest.TestCase):
    def test_all_signed_digits_and_recombination(self):
        for width in (5, 6):
            for integer in range(-(1 << (width - 1)), 1 << (width - 1)):
                self.assertEqual(combine(digits(integer, width)), integer)

    def test_domain_endpoints(self):
        self.assertEqual(digits(-16, 5), [0, 0, 0, 0, 0])
        self.assertEqual(digits(15, 5), [1, 1, 1, 1, 1])
        for integer in (-17, 16):
            with self.assertRaises(ValueError):
                digits(integer, 5)

    def test_clean_fixed_points_and_convergence(self):
        for digit in (0, 1):
            self.assertEqual(h1(digit), digit)
            for noise in ("-0.125", "-0.01", "0.01", "0.125"):
                error = abs(mp.mpf(noise))
                value = digit + mp.mpf(noise)
                for _ in range(3):
                    value = h1(value)
                    self.assertLessEqual(abs(value - digit), 5 * error * error)
                    self.assertLess(abs(value - digit), error)
                    error = abs(value - digit)

    def test_thrifty_physical_units(self):
        delta = mp.mpf(1 << 30)
        value = mp.mpf("1.000125")
        raw = delta * value
        square, cube = raw**2, raw**3
        rescaled = (3 * delta * square - 2 * cube) / delta
        self.assertLess(abs(rescaled / delta**2 - h1(value)), mp.mpf("1e-90"))
        self.assertGreater(abs(rescaled / delta - h1(value)), 1)

    def test_bypass_units_and_representative(self):
        gap, integer, message = mp.mpf(4096), -13, mp.mpf("0.375")
        high = integer + message / gap
        self.assertEqual((high - combine(digits(integer, 5))) * gap, message)
        self.assertGreater(abs((high - (integer + 1)) * gap - message), 1)
        self.assertGreater(abs(high - integer - message), mp.mpf("0.1"))

    def test_indirect_oracle(self):
        for integer in (-16, -1, 0, 15):
            observed = indirect_exact(integer, 5)
            for value, truth in zip(observed, digits(integer, 5)):
                self.assertLess(abs(value - truth), mp.mpf("1e-90"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--known-answers", action="store_true")
    args = parser.parse_args()
    if args.manifest:
        manifest = json.loads(args.manifest.read_text())
        result = known_answers(manifest, args.manifest.name) if args.known_answers else validate(manifest)
        if args.known_answers:
            result["manifest_sha256"] = hashlib.sha256(args.manifest.read_bytes()).hexdigest()
        text = json.dumps(result, indent=2) + "\n"
        if args.output:
            args.output.write_text(text)
        print(text)
    else:
        unittest.main(argv=["discrete_identity_reference"], verbosity=2)
