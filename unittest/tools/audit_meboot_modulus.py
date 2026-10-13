#!/usr/bin/env python3
"""Check the public CtS modulus schedule and key-modulus boundary."""
import json
import math
from pathlib import Path


def audit(path):
    config = json.loads(path.read_text())
    levels = config["level_config"]
    key_level = config.get("max_key_switch_level", len(levels) - 1)
    prefix_level = key_level + 1
    assert prefix_level == len(levels) - 1, f"{path.name}: missing separate OverModRaise level"
    key_main, key_terminal = levels[key_level]
    assert key_main == max(main for main, _ in levels[:key_level + 1])
    assert key_terminal == max(terminal for _, terminal in levels[:key_level + 1]), \
        "reported key ceiling must cover every normal key component"
    prefix_main, prefix_terminal = levels[prefix_level]
    assert prefix_main == key_main and prefix_terminal > key_terminal, "prefix must add only temporary primes"
    standard_end = key_level - config["num_cts_levels"]
    meboot_end = prefix_level - 3
    assert config["num_cts_levels"] == 4
    assert key_level - meboot_end == 2, "MEBOOT must consume two main-chain levels"
    assert meboot_end - standard_end == 2, "paired output must retain two extra levels"
    assert config["sparse_hamming_weight"] == 256
    assert config["num_double_angle"] == 5
    assert levels[key_level] != levels[key_level - 1], "normal CtS must rescale within its key-safe chain"
    key_primes = config["main_primes"][:key_main] + config["terminal_primes"][:key_terminal]
    log_q = sum(math.log2(q) for q in key_primes)
    log_p = sum(math.log2(p) for p in config["auxiliary_primes"])
    basis_bits = [sum(math.log2(q) for q in
                     config["main_primes"][:main] + config["terminal_primes"][:terminal])
                  for main, terminal in levels]
    rescale_bits = [0] + [upper - lower for upper, lower in zip(basis_bits[1:], basis_bits)]
    evalmod_scales = {}
    for route, start in (("standard", standard_end), ("meboot", meboot_end)):
        scale = round(rescale_bits[start]) / 2**11 + sum(
            rescale_bits[start - i] / 2**(i + 1) for i in range(11))
        steps = [scale]
        for i in range(11):
            scale = 2 * scale - rescale_bits[start - i]
            steps.append(scale)
        assert min(steps) >= 53, f"{path.name}: {route} EvalMod scale loses precision"
        evalmod_scales[route] = {"start_bits": steps[0], "min_bits": min(steps), "end_bits": steps[-1]}
    prefix_primes = config["terminal_primes"][key_terminal:prefix_terminal]
    prefix_bits = sum(math.log2(q) for q in prefix_primes)
    assert (59 <= prefix_bits <= 61) if path.name == "bootparam_40.json" else \
           (54 <= prefix_bits <= 56), "prefix budget differs from the qualified preset"
    outputs = (standard_end - 11 - config["num_stc_levels"],
               meboot_end - 11 - config["num_stc_levels"])
    expected_outputs = (2, 4) if path.name == "bootparam_40.json" else (4, 6)
    assert outputs == expected_outputs, "output depth differs from the qualified preset"
    return {"profile": path.name, "key_level": key_level,
            "prefix_level": prefix_level, "standard_cts_end": standard_end,
            "meboot_cts_end": meboot_end, "main_chain_levels_saved": 2,
            "key_log_q": log_q, "key_log_p": log_p, "key_log_qp": log_q + log_p,
            "temporary_log_q": prefix_bits,
            "effective_subring_degree": 2 ** config["log_degree"] // 16,
            "subring_hamming_weight": config["sparse_hamming_weight"],
            "subring_entry_sigma": 819.2, "dense_sigma": 3.2,
            "evalmod_K": 64, "evalmod_levels": 11,
            "evalmod_scales": evalmod_scales,
            "standard_output": outputs[0],
            "meboot_output": outputs[1],
            "security_classification": "modulus-boundary audit; separate heuristic estimator evidence"}


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[2]
    print(json.dumps([audit(root / "parameters" / name) for name in
                      ("bootparam_40.json", "bootparam_40_64bit.json")], indent=2))
