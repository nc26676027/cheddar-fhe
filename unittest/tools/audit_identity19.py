#!/usr/bin/env python3
"""Check the selected HW40 domain and optionally bind prior lattice evidence."""
import argparse
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def tail_log2(h, threshold, union_bits=18):
    """Chernoff bound for H+1 independent centered unit uniforms."""
    n = h + 1
    if threshold >= n / 2:
        return -math.inf
    lo, hi = 1e-8, 10000.0
    for _ in range(150):
        t = (lo + hi) / 2
        if 0.5 / math.tanh(t / 2) - 1 / t < threshold / n:
            lo = t
        else:
            hi = t
    t = (lo + hi) / 2
    log_mgf = t / 2 + math.log1p(-math.exp(-t)) - math.log(t)
    return union_bits - (t * threshold - n * log_mgf) / math.log(2)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit(security_package=None):
    path = ROOT / "parameters/identity19_meboot.json"
    p = json.loads(path.read_text())
    assert p["subring_h"] == 40 and p["subring_extension"] == 8
    assert p["entry_q0_override"] == p["q"][0] == 4398044938241
    assert p["entry_p_override"] == [786433]
    assert p["entry_digit_bits"] == 4 and p["entry_error_sigma"] == 3.2
    assert (p["mod1"]["K"], p["mod1_actual_degree"], p["mod1"]["DoubleAngle"]) == (20, 62, 2)
    assert p["mod1"]["LevelQ"] == 29 and p["output_level"] == 21
    n = (1 << p["log_degree"]) // p["subring_extension"]
    entry_qp = p["q"][0] * math.prod(p["entry_p_override"])
    # Include half an integer cell and a 2^-10 fractional guard.
    threshold = p["mod1"]["K"] - 0.5 - 2**-10
    bound = tail_log2(p["subring_h"], threshold)
    assert bound < -128
    report = {
        "profile_sha256": sha256(path),
        "effective_subring_dimension": n,
        "subring_h": p["subring_h"],
        "entry_qp": str(entry_qp),
        "entry_log2_qp": math.log2(entry_qp),
        "failure_model": "H+1 independent centered unit uniforms; conditional Chernoff bound",
        "whole_refresh_union_factor": 2**18,
        "summands": p["subring_h"] + 1,
        "threshold": threshold,
        "log2_failure_bound_with_guard": bound,
        "log2_failure_bound_at_K": tail_log2(p["subring_h"], p["mod1"]["K"]),
    }
    if security_package:
        package = Path(security_package)
        summary_path = package / "security-resolved.json"
        summary = json.loads(summary_path.read_text())
        # This immutable upstream package predates the Identity19 rename.
        case = next(c for c in summary["configurations"]
                    if c["configuration"] == "identity16-18-joint")
        plan_path = package / case["plan"]
        assert sha256(plan_path) == case["plan_sha256"]
        plan = json.loads(plan_path.read_text())
        assert plan["q"] == p["q"] and plan["p"] == p["p"]
        assert plan["contract"]["dense_secret_hw"] == p["dense_hamming_weight"]
        domains = {}
        for domain, dimension, h, q in [
            ("entry", n, p["subring_h"], entry_qp),
            ("main", 1 << p["log_degree"], p["dense_hamming_weight"],
             math.prod(p["q"] + p["p"])),
        ]:
            records = [r for r in case["results"] if r["domain"] == domain]
            assert len(records) == 6
            attacks = []
            for record in records:
                record_path = package / record["file"]
                assert sha256(record_path) == record["sha256"]
                evidence = json.loads(record_path.read_text())
                assert (evidence["n"], evidence["H"], int(evidence["q"])) == (dimension, h, q)
                assert evidence["Xe"] == "DiscreteGaussian(stddev=3.2)"
                assert record["status"] == "computed" and record["finite"]
                assert float(record["log2_rop"]) >= 128
                attacks.append({k: record[k] for k in ("attack", "log2_rop", "file", "sha256")})
            domains[domain] = {
                **case["domains"][domain],
                "attacks": attacks,
            }
        report["lattice_evidence"] = {
            "source_package": str(package),
            "source_configuration": case["configuration"],
            "summary_sha256": sha256(summary_path),
            "plan_sha256": sha256(plan_path),
            "classification": "reused exact-domain classical heuristic screens; not a new estimator run",
            "estimator_revision": summary["estimator_revision"],
            "domains": domains,
        }
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--security-package", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(audit(args.security_package), indent=2) + "\n"
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end="")
