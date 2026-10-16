"""Bind the Discrete candidate to exact public parameters and polynomial evidence.

This is a parameter/domain audit, not a fresh lattice-estimator run or a
probability bound on encrypted arithmetic errors.
"""

import argparse
import hashlib
import json
import math
import re
import subprocess
from fractions import Fraction
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


def audit(profile_log, certificate_path):
    profile_path = "parameters/identity19_meboot.json"
    manifest_path = "parameters/research/discrete_direct6_d95.json"
    security_path = "docs/evidence/identity19/security.json"
    profile = json.loads((ROOT / profile_path).read_text())
    log_text = profile_log.read_text()
    schedule = re.search(
        r"schedule_alignment_level=(\d+) basis_bits=(\d+) "
        r"extraction_bits=(\d+) output_level=(\d+)", log_text)
    if not schedule or "[  PASSED  ] 1 test." not in log_text:
        raise ValueError("schedule must come from a passing fresh profile log")
    alignment, basis_bits, extraction_bits, output_level = map(int, schedule.groups())
    input_match = re.search(r"stage=cleaner-input-real level=(\d+)", log_text)
    extraction_match = re.search(
        r"stage=cleaner-input-real/extract/0/0 level=(\d+)", log_text)
    if not input_match or not extraction_match:
        raise ValueError("missing observed extractor input/output levels")
    extraction_input = int(input_match[1])
    extraction_level = int(extraction_match[1])
    affine_rescales = alignment - extraction_input
    if affine_rescales not in (0, 1) or extraction_input - extraction_level != 7:
        raise ValueError("invalid affine or extractor level observations")
    cleaning_stages = re.findall(
        r"stage=cleaner-input-real/clean/0/(\d+) level=(\d+)", log_text)
    if len(cleaning_stages) != 2:
        raise ValueError("missing cubic level observations")
    level = extraction_level
    scale = float(2**extraction_bits)
    ledger = []
    for iteration, observed in cleaning_stages:
        observed = int(observed)
        count = level - observed
        if count not in (1, 2):
            raise ValueError("invalid observed cubic rescale schedule")
        scale = round(scale)
        before = scale
        primes = profile["q"][observed + 1:level + 1]
        scale = scale**3 / math.prod(primes)
        ledger.append({
            "iteration": int(iteration), "input_level": level,
            "output_level": observed, "rescale_count": count,
            "input_scale": before, "output_scale": scale,
            "rescale_primes": primes,
        })
        level = observed
    if level - 1 != output_level:
        raise ValueError("observed final output does not match the schedule")
    manifest = json.loads((ROOT / manifest_path).read_text())
    certificate = json.loads((ROOT / certificate_path).read_text())
    security = json.loads((ROOT / security_path).read_text())
    source = (ROOT / "src/extension/BootParameter.cpp").read_text()
    selected = source.split("if (route == BootRoute::kMEBoot) {", 1)[1]
    selected = selected.split("return out;", 1)[0]
    for name in ("q", "p"):
        match = re.search(r"out\." + name + r" = \{([^}]+)\}", selected)
        values = [int(value.strip()) for value in match[1].split(",")]
        if values != profile[name]:
            raise ValueError(f"runtime {name} differs from the frozen profile")
    for name, field in (("extra_q", "proper_extra_q"), ("sparse_h", "subring_h"),
                        ("extension", "subring_extension")):
        match = re.search(r"out\." + name + r" = (\d+);", selected)
        if int(match[1]) != profile[field]:
            raise ValueError(f"runtime {name} differs from the frozen profile")
    if security["profile_sha256"] != digest(profile_path):
        raise ValueError("security evidence is not bound to the exact profile")
    if certificate["manifest_sha256"] != digest(manifest_path):
        raise ValueError("certificate is not bound to the exact coefficient manifest")
    if not certificate["extraction_contract_passed"]:
        raise ValueError("extractor certificate failed")
    if (manifest["width"], manifest["degree"]) != (6, 95):
        raise ValueError("unexpected six-bit candidate")
    q0, h = profile["q"][0], profile["subring_h"]
    threshold = Fraction(39, 2) - Fraction(1, 1024)
    terms = h + 1
    a = Fraction(terms, 2) - threshold
    tail = 2 * sum(
        (-1)**j * math.comb(terms, j) * (a - j)**terms
        for j in range(math.floor(a) + 1)
    ) / math.factorial(terms)
    q_product, p_product = math.prod(profile["q"]), math.prod(profile["p"])
    entry_qp = q0 * profile["entry_p_override"][0]
    domains = security["lattice_evidence"]["domains"]
    if int(domains["main"]["q"]) != q_product * p_product:
        raise ValueError("main public QP differs from the screened domain")
    if int(domains["entry"]["q"]) != entry_qp:
        raise ValueError("entry public QP differs from the screened domain")
    return {
        "schema": 1,
        "classification": "exact public-domain reuse audit; no new estimator run",
        "source": {
            "git_head": subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "branch": subprocess.check_output(
                ["git", "branch", "--show-current"], cwd=ROOT, text=True).strip(),
            "working_sources_bound_by_sha256": True,
            "schedule_log": str(profile_log),
            "schedule_log_sha256": hashlib.sha256(profile_log.read_bytes()).hexdigest(),
        },
        "files_sha256": {path: digest(path) for path in (
            profile_path, manifest_path, certificate_path, security_path,
            "src/extension/BootParameter.cpp",
            "src/extension/DiscreteIdentityBootstrap.cpp",
            "src/extension/DiscreteIntegerCleaner.cpp",
            "src/extension/EvalPoly.cpp",
            "include/extension/EvalPoly.h",
            "include/extension/DiscreteIdentityBootstrap.h",
            "include/extension/DiscreteIntegerCleaner.h",
            "include/extension/MEBootCtS.h",
            "src/extension/MEBootExecution.cpp",
            "src/extension/MEBootCtS.cu",
            "src/extension/BootTransforms.cpp",
            "src/extension/BootContext.cpp",
            "src/core/EntryGadget.cu",
            "src/UserInterface.cu",
            "unittest/MEBootDiscreteIdentityTest.cpp",
            "unittest/DiscreteIntegerCleanerTest.cpp",
            "unittest/CMakeLists.txt",
            "unittest/fixtures/discrete-direct6-known-answers.json",
            "tools/research/generate_discrete_extractor.py",
            "tools/research/certify_discrete_extractor.py",
            "tools/research/discrete_identity_reference.py",
            "tools/research/audit_meboot_discrete.py",
            "tools/research/summarize_meboot_discrete.py",
            "tools/research/profile_discrete_depth.py",
            "tools/research/summarize_discrete_depth_campaign.py",
        )},
        "public_parameters": {
            "logN": 16, "subring_degree": 8192,
            "dense_h": 2048, "subring_h": h, "extension": 8,
            "secret_law": "fixed-weight ternary, independent uniform signs",
            "entry_error_sigma": profile["entry_error_sigma"],
            "q": profile["q"], "p": profile["p"],
            "q_extra": profile["proper_extra_q"], "entry_p": profile["entry_p_override"],
            "main_qp": str(q_product * p_product), "entry_qp": str(entry_qp),
            "entry_digit_bits": 4, "entry_rows": 11,
            "entry_structured_equations": 11 * 65536,
            "main_sample_model": "unlimited; added dense rotations do not increase QP",
        },
        "modulus_chain": {
            "level_definition": "Q_L = product(q0,...,qL)",
            "main_primes": [
                {"level": index, "prime": prime,
                 "prime_bit_length": prime.bit_length(),
                 "prime_log2": math.log2(prime),
                 "Q_log2": sum(math.log2(q) for q in profile["q"][:index + 1])}
                for index, prime in enumerate(profile["q"])
            ],
            "after_low_cts": {
                "restriction": {
                    "input_level": 29, "output_level": alignment,
                    "discarded_primes": profile["q"][alignment + 1:30],
                },
                "fused_affine": {
                    "input_level": alignment, "output_level": extraction_input,
                    "rescale_prime": profile["q"][alignment] if affine_rescales else None,
                },
                "digit_extract": {
                    "input_level": extraction_input,
                    "output_level": extraction_level,
                    "rescale_primes": profile["q"][extraction_level + 1:extraction_input + 1],
                },
                "cleaning": ledger,
                "normalize": {
                    "input_level": level, "output_level": output_level,
                    "rescale_prime": profile["q"][level],
                },
            },
            "output_Q_log2": sum(math.log2(q) for q in profile["q"][:output_level + 1]),
            "recovered_levels_vs_L7": output_level - 7,
            "retained_modulus_bits_vs_L7": sum(
                math.log2(q) for q in profile["q"][8:output_level + 1]),
        },
        "extractor": {
            "width": 6, "degree": 95, "depth": 7,
            "signed_centers": [-32, 31], "input_radius": "2^-12",
            "coefficient_source": "independent sampled minimax; exact rational certificate",
            "complex_and_encrypted_error_in_certificate": False,
        },
        "scaling": {
            "stc_gain": "1/64", "stc_output_scale": "2^33",
            "lift_multiplier": 32768, "lift_scale": "2^48",
            "integer_units_in_lift": q0 / 2**33,
            "message_unit_gap": q0 / (2**33 / 64),
            "cleaner_alignment_level": alignment,
            "normalized_cleaner_input": "(I+0.5)/32",
            "cleaner_input_scale": f"2^{basis_bits}",
            "extracted_digit_scale": f"2^{extraction_bits}",
            "extraction_input_level": extraction_input,
            "extraction_output_level": extraction_level,
            "cleaning_iterations": 2, "cleaning_ledger": ledger,
            "cleaner_output_log_scale": math.log2(scale),
            "normalization_encoded_integer": (
                q0 / (2**33 / 64) * profile["q"][level] * 2**40 / scale),
            "output_level": output_level, "output_scale": "2^40",
        },
        "domain": {
            "centered_component_phase_bound_over_q0": str(Fraction(h + 1, 2)),
            "integer_bound": [-21, 21],
            "representation_domain": [-32, 31],
            "integer_bound_assumptions": (
                "one common centered Q0 lift, fixed-weight ternary subring H40, "
                "residual phase centered in [-q0/2,q0/2]; I=(lifted_phase-residual)/q0"
            ),
            "six_bit_representation_covers_integer_bound": True,
            "fractional_radius_and_CtS_error": (
                "separate encrypted numerical gate; not proved by the integer bound"
            ),
            "K20_reference_model": "41 independent centered unit uniforms, conditional only",
            "K20_reference_threshold": str(threshold),
            "K20_reference_log2_single_tail": math.log2(tail),
            "K20_reference_log2_union_2pow18": math.log2(tail) + 18,
            "K20_reference_covers_entry_noise": False,
        },
        "lattice": {
            "source_evidence": security_path,
            "estimator_revision": security["lattice_evidence"]["estimator_revision"],
            "main": domains["main"]["minimum_finite"],
            "entry": domains["entry"]["minimum_finite"],
            "qualification": (
                "exact Q/P, entry modulus and secret/error domain reuse; heuristic "
                "screens under the inherited model, not an IND-CPA proof"
            ),
        },
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile-log", type=Path, required=True)
    parser.add_argument("--certificate", default=(
        "docs/evidence/discrete-identity/direct6-certificate.json"))
    args = parser.parse_args()
    result = audit(args.profile_log, args.certificate)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Exact public domains and six-bit certificate match; audit:", args.output)
