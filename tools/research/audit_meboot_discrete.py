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


def audit():
    profile_path = "parameters/identity19_meboot.json"
    manifest_path = "parameters/research/discrete_direct6_d95.json"
    certificate_path = "docs/evidence/discrete-identity/direct6-certificate.json"
    security_path = "docs/evidence/identity19/security.json"
    profile = json.loads((ROOT / profile_path).read_text())
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
        },
        "files_sha256": {path: digest(path) for path in (
            profile_path, manifest_path, certificate_path, security_path,
            "src/extension/BootParameter.cpp",
            "src/extension/DiscreteIdentityBootstrap.cpp",
            "src/extension/DiscreteIntegerCleaner.cpp",
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
            "cleaner_input_scale": "2^40", "extracted_digit_scale": "2^43",
            "cleaning_iterations": 2, "cleaner_output_log_scale_approx": 67,
            "output_level": 7, "output_scale": "2^40",
        },
        "domain": {
            "centered_component_phase_bound_over_q0": str(Fraction(h + 1, 2)),
            "integer_bound": [-21, 21],
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
    args = parser.parse_args()
    result = audit()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Exact public domains and six-bit certificate match; audit:", args.output)
