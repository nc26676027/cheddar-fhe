#!/usr/bin/env python3
"""Run a configuration-specific heuristic screen with the paper's attack adapter.

Invoke with Sage's Python. Dense MITM is optional and omitted rows remain
explicitly incomplete; infinite costs never count as a security certificate.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--estimator", type=Path, required=True)
    parser.add_argument("--attack-adapter", type=Path, required=True)
    parser.add_argument("--dense-mitm", action="store_true")
    parser.add_argument("--profile", default="bootparam_40.json")
    parser.add_argument("--domain", choices=("entry", "main"), required=True)
    parser.add_argument("--attack", choices=(
        "usvp", "bdd", "dual", "bdd_hybrid", "dual_hybrid", "bdd_mitm_hybrid"),
        required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.estimator.resolve()))
    from sage.all import ZZ, oo, RealField
    from estimator import LWE, ND, RC
    spec = importlib.util.spec_from_file_location("paper_attack_adapter", args.attack_adapter)
    adapter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(adapter)
    profile = Path(__file__).resolve().parents[2] / "parameters" / args.profile
    config = json.loads(profile.read_text())
    degree = 1 << config["log_degree"]
    if args.domain == "entry":
        nm, nt = (x - y for x, y in zip(
            config["level_config"][0], config.get("additional_base", (0, 0))))
        primes = (config["main_primes"][:nm] + config["terminal_primes"][:nt] +
                  config["auxiliary_primes"][:nm + nt])
        n, h, sigma = degree // 16, config["sparse_hamming_weight"], 819.2
    else:
        normal_levels = config["level_config"][:config["max_key_switch_level"] + 1]
        nm = max(main for main, _ in normal_levels)
        nt = max(terminal for _, terminal in normal_levels)
        primes = (config["main_primes"][:nm] + config["terminal_primes"][:nt] +
                  config["auxiliary_primes"])
        n, h, sigma = degree, config["dense_hamming_weight"], 3.2
    record = {
        "profile": args.profile, "domain": args.domain, "attack": args.attack,
        "profile_sha256": hashlib.sha256(profile.read_bytes()).hexdigest(),
        "adapter_sha256": hashlib.sha256(args.attack_adapter.read_bytes()).hexdigest(),
        "estimator_revision": subprocess.check_output(
            ["git", "-C", str(args.estimator), "rev-parse", "HEAD"], text=True).strip(),
        "n": n, "h": h, "sigma": sigma, "samples": "unlimited",
        "error_model": "DiscreteGaussian; client uses rounded, six-sigma-clamped samples",
        "classification": "heuristic screen, not a security certificate",
    }
    if args.domain == "main" and args.attack == "bdd_mitm_hybrid" and not args.dense_mitm:
        record.update(status="INCOMPLETE", reason="dense MITM requires --dense-mitm")
    else:
        q = ZZ(1)
        for prime in primes:
            q *= prime
        record["log2_qp"] = float(RealField(256)(q).log2())
        params = LWE.Parameters(n=n, q=q, Xs=ND.SparseBinomial(1, h, n),
                                Xe=ND.DiscreteGaussian(sigma), m=oo)
        function, kwargs = adapter.attack_call(LWE, args.attack)
        context, _ = adapter.attack_execution_context(args.attack, params, ND)
        with context:
            cost = function(params, red_cost_model=RC.BDGL16, **kwargs)
        rop = cost["rop"]
        record.update(status="COMPUTED" if rop != oo else "UNAVAILABLE",
                      log2_rop=float(RealField(256)(rop).log2()) if rop != oo else None)
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    main()
