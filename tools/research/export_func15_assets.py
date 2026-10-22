import os
import sys
import json
import math
import struct
import subprocess
import numpy as np
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
PAPER_ROOT = Path("/mnt/d/code space/paper1-linear-bootstrap")
if not PAPER_ROOT.exists():
    PAPER_ROOT = Path(r"D:\code space\paper1-linear-bootstrap")

print(f"Repo root: {REPO_ROOT}")
print(f"Paper root: {PAPER_ROOT}")

# 1. Load func15-selected-scalar-model.json
model_path = PAPER_ROOT / "paper/evidence/measurements/selected-bootstrap-and-cnn-20261005/func15-selected-model/func15-selected-scalar-model.json"
print(f"Loading scalar model from {model_path}...")
with open(model_path, "r", encoding="utf-8") as f:
    scalar_model = json.load(f)

contract = scalar_model["contract"]
mod1_raw = scalar_model["mod1"]

# 2. Export parameters/func15_meboot.json
func15_param = {
    "schema": "cheddar-func15-parameters/v1",
    "name": "Func15Selected-q34-H80-K22-DA2-P2",
    "log_degree": contract["logN"],
    "degree": 1 << contract["logN"],
    "slots": 1 << (contract["logN"] - 1),
    "main_primes": contract["q"],
    "aux_primes": contract["p"],
    "entry_primes": contract["entry_p"],
    "entry_q0": contract["entry_q0"],
    "subring_H": contract["subring_H"],
    "subring_extension": contract["subring_extension"],
    "subring_Nprime": (1 << contract["logN"]) // contract["subring_extension"],
    "dense_H": contract["dense_H"],
    "widths": contract["widths"],
    "bases": contract["bases"],
    "K": contract["K"],
    "mod1_degree_actual": contract["mod1_degree_actual"],
    "double_angle": contract["double_angle"],
    "input_level": 2,
    "input_scale_log2": 43,
    "output_level": contract["output_level"],
    "output_scale_log2": contract["output_scale_log2"],
    "activation_gain": contract["activation_gain"],
    "activation_output_shift": scalar_model.get("activation_output_shift", 0.0546875),
    "gain": scalar_model.get("gain", 2.0),
    "message_ratio": scalar_model.get("message_ratio", 8.0),
    "branch_gain": scalar_model.get("branch_gain", 1.0000114441645567),
    "qdiff": mod1_raw["QDiff"],
    "sqrt2pi": mod1_raw["Sqrt2Pi"],
    "physical_domain": contract["physical_domain"],
    "normalized_domain": [-0.25, 0.25]
}

out_param_path = REPO_ROOT / "parameters/func15_meboot.json"
out_param_path.parent.mkdir(parents=True, exist_ok=True)
with open(out_param_path, "w", encoding="utf-8") as f:
    json.dump(func15_param, f, indent=2)
print(f"Saved {out_param_path}")

# 3. Export parameters/func15_rboot_polynomial.json
def extract_coeffs(poly):
    return [0.0 if c is None else float(c[0]) for c in poly["Coeffs"]]

main_coeffs = extract_coeffs(mod1_raw["Mod1Poly"])
inv_coeffs = extract_coeffs(mod1_raw["Mod1InvPoly"])
arc_coeffs = extract_coeffs(mod1_raw["Mod1ArcCosPoly"])

q0 = float(contract["q"][0])
internal = 2.0 ** float(mod1_raw["LogDefaultScale"])
message_ratio = float(scalar_model["message_ratio"])
final_ratio = max(q0, internal) / (message_ratio * internal)
k = float(mod1_raw["K"])
qdiff = float(mod1_raw["QDiff"])
c2s = min(1.0, internal / 2.0 ** round(math.log2(q0))) / (k * qdiff)
input_gain = message_ratio * c2s * final_ratio
offset = -0.25 / k
gain = float(scalar_model["gain"])
output_shift = float(scalar_model.get("activation_output_shift", 0.0546875))
sqrt2pi = float(mod1_raw["Sqrt2Pi"])

func15_poly = {
    "schema": "cheddar-func15-rboot-polynomial/v1",
    "main_chebyshev_coeffs": main_coeffs,
    "odd_tail_coeffs": inv_coeffs,
    "even_arc_chebyshev_coeffs": arc_coeffs,
    "constants": {
        "K": k,
        "QDiff": qdiff,
        "Sqrt2Pi": sqrt2pi,
        "DoubleAngle": int(mod1_raw["DoubleAngle"]),
        "final_ratio": final_ratio,
        "c2s": c2s,
        "input_gain": input_gain,
        "offset": offset,
        "gain": gain,
        "output_shift": output_shift,
        "activation_gain": float(contract["activation_gain"])
    }
}

out_poly_path = REPO_ROOT / "parameters/func15_rboot_polynomial.json"
with open(out_poly_path, "w", encoding="utf-8") as f:
    json.dump(func15_poly, f, indent=2)
print(f"Saved {out_poly_path}")

# Verify with Python implementation against samples table
samples = np.asarray(scalar_model["samples"]["values"], dtype=np.float64)
x_vals, act_target, id_target = samples[:, 0], samples[:, 1], samples[:, 2]

def eval_cosine(x):
    arg = x * input_gain + offset
    z = np.polynomial.chebyshev.chebval(arg, main_coeffs)
    c = sqrt2pi
    for _ in range(int(mod1_raw["DoubleAngle"])):
        c *= c
        z = 2.0 * z * z - c
    return z, c

def eval_activation(x):
    z, c = eval_cosine(x)
    odd = np.polynomial.polynomial.polyval(z, inv_coeffs)
    cosine = 2.0 * z * z - c
    arc = np.polynomial.chebyshev.chebval(cosine, arc_coeffs)
    return final_ratio * (gain * (2.0 * odd + arc) + 0.0625) + output_shift

def eval_identity(x):
    z, _ = eval_cosine(x)
    odd = np.polynomial.polynomial.polyval(z, inv_coeffs)
    return final_ratio * gain * 4.0 * odd

act_pred = eval_activation(x_vals)
id_pred = eval_identity(x_vals)
max_act_err = float(np.max(np.abs(act_pred - act_target)))
max_id_err = float(np.max(np.abs(id_pred - id_target)))
print(f"Validation on {len(samples)} samples:")
print(f"  Activation max abs error: {max_act_err:.4e}")
print(f"  Identity max abs error:   {max_id_err:.4e}")
assert max_act_err < 1e-10, "Activation error exceeds tolerance!"
assert max_id_err < 1e-10, "Identity error exceeds tolerance!"

# Generate C++ inlined header: include/extension/Func15RBootCoefficients.inc
header_lines = [
    "// Autogenerated from paper1-linear-bootstrap func15-selected-scalar-model.json",
    "// Certified RBOOT composite minimax activation polynomial coefficients",
    "#pragma once",
    "",
    "namespace cheddar {",
    "namespace func15_rboot {",
    "",
    f"constexpr double kK = {k};",
    f"constexpr double kQDiff = {qdiff};",
    f"constexpr double kSqrt2Pi = {sqrt2pi};",
    f"constexpr int kDoubleAngle = {int(mod1_raw['DoubleAngle'])};",
    f"constexpr double kFinalRatio = {final_ratio};",
    f"constexpr double kC2S = {c2s};",
    f"constexpr double kInputGain = {input_gain};",
    f"constexpr double kOffset = {offset};",
    f"constexpr double kGain = {gain};",
    f"constexpr double kOutputShift = {output_shift};",
    f"constexpr double kActivationGain = {float(contract['activation_gain'])};",
    "",
    f"constexpr int kMainDegree = {len(main_coeffs) - 1};",
    "inline const double kMainChebyshevCoeffs[] = {"
]
for i, c in enumerate(main_coeffs):
    header_lines.append(f"    {c:.17g}," if i < len(main_coeffs)-1 else f"    {c:.17g}")
header_lines.append("};")
header_lines.append("")
header_lines.append(f"constexpr int kOddTailDegree = {len(inv_coeffs) - 1};")
header_lines.append("inline const double kOddTailCoeffs[] = {")
for i, c in enumerate(inv_coeffs):
    header_lines.append(f"    {c:.17g}," if i < len(inv_coeffs)-1 else f"    {c:.17g}")
header_lines.append("};")
header_lines.append("")
header_lines.append(f"constexpr int kEvenArcDegree = {len(arc_coeffs) - 1};")
header_lines.append("inline const double kEvenArcChebyshevCoeffs[] = {")
for i, c in enumerate(arc_coeffs):
    header_lines.append(f"    {c:.17g}," if i < len(arc_coeffs)-1 else f"    {c:.17g}")
header_lines.append("};")
header_lines.append("")
header_lines.append("}  // namespace func15_rboot")
header_lines.append("}  // namespace cheddar")

inc_path = REPO_ROOT / "include/extension/Func15RBootCoefficients.inc"
inc_path.parent.mkdir(parents=True, exist_ok=True)
with open(inc_path, "w", encoding="utf-8") as f:
    f.write("\n".join(header_lines) + "\n")
print(f"Saved inlined C++ constants to {inc_path}")

# 4. Extract CIFAR-10 test image index 0 and golden logits
golden_ref_path = PAPER_ROOT / "paper/evidence/measurements/selected-bootstrap-and-cnn-20261005/cnn-selected-reference/func15-selected-polynomial.json"
with open(golden_ref_path, "r", encoding="utf-8") as f:
    ref_data = json.load(f)

label0 = ref_data["labels"][0]
pred0 = ref_data["predictions"][0]
logits0 = ref_data["logits"][0]

golden_logits_obj = {
    "index": 0,
    "true_label": int(label0),
    "predicted_label": int(pred0),
    "logits": logits0
}
fixtures_dir = REPO_ROOT / "unittest/fixtures"
fixtures_dir.mkdir(parents=True, exist_ok=True)
out_golden_path = fixtures_dir / "resnet20_golden_logits_index0.json"
with open(out_golden_path, "w", encoding="utf-8") as f:
    json.dump(golden_logits_obj, f, indent=2)
print(f"Saved {out_golden_path}: label={label0}, pred={pred0}")

# Extract image 0 from git git show remotes/origin/artifact:inputs/values.txt
# Read the first 3072 floats
print("Extracting CIFAR-10 test image 0 from artifact branch...")
proc = subprocess.Popen(
    ["git", "-C", str(PAPER_ROOT), "show", "remotes/origin/artifact:inputs/values.txt"],
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True
)
image0_floats = []
for line in proc.stdout:
    for token in line.split():
        image0_floats.append(float(token))
        if len(image0_floats) == 3072:
            break
    if len(image0_floats) == 3072:
        break
proc.kill()
assert len(image0_floats) == 3072, f"Expected 3072 floats, got {len(image0_floats)}"

# Save image 0 in binary (float64) and json
cifar0_bin_path = fixtures_dir / "cifar10_index0.bin"
with open(cifar0_bin_path, "wb") as f:
    f.write(struct.pack(f"{len(image0_floats)}d", *image0_floats))
print(f"Saved {cifar0_bin_path} ({len(image0_floats)*8} bytes)")

cifar0_json_path = fixtures_dir / "cifar10_index0.json"
with open(cifar0_json_path, "w", encoding="utf-8") as f:
    json.dump({
        "index": 0,
        "label": int(label0),
        "shape": [3, 32, 32],
        "values": image0_floats
    }, f)
print(f"Saved {cifar0_json_path}")

# 5. Extract and fold ResNet-20 checkpoint from artifact branch
print("Extracting and folding ResNet-20 checkpoint weights from artifact branch...")

def get_git_file(relpath):
    cmd = ["git", "-C", str(PAPER_ROOT), "show", f"remotes/origin/artifact:{relpath}"]
    p = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return [float(x) for x in p.stdout.split()]

# BatchNorm epsilon
EPSILON = 1e-5

def fold_bn(conv_name, bn_prefix, out_ch, in_ch):
    flat_w = get_git_file(f"inputs/checkpoint/{conv_name}")
    gamma = get_git_file(f"inputs/checkpoint/{bn_prefix}_weight.txt")
    beta = get_git_file(f"inputs/checkpoint/{bn_prefix}_bias.txt")
    mean = get_git_file(f"inputs/checkpoint/{bn_prefix}_running_mean.txt")
    var = get_git_file(f"inputs/checkpoint/{bn_prefix}_running_var.txt")
    
    assert len(flat_w) == out_ch * in_ch * 9
    assert len(gamma) == out_ch
    assert len(beta) == out_ch
    assert len(mean) == out_ch
    assert len(var) == out_ch

    folded_w = np.zeros((out_ch, in_ch, 3, 3), dtype=np.float64)
    folded_b = np.zeros((out_ch,), dtype=np.float64)

    idx = 0
    for o in range(out_ch):
        gain = gamma[o] / math.sqrt(var[o] + EPSILON)
        folded_b[o] = beta[o] - mean[o] * gain
        for i in range(in_ch):
            for h in range(3):
                for w in range(3):
                    folded_w[o, i, h, w] = flat_w[idx] * gain
                    idx += 1
    return folded_w, folded_b

folded_layers = {}

# Stem: 16 out, 3 in
print("Folding Stem layer...")
stem_w, stem_b = fold_bn("conv1_weight.txt", "bn1", 16, 3)
folded_layers["stem"] = {
    "weight": stem_w.flatten().tolist(),
    "bias": stem_b.tolist(),
    "in_ch": 3,
    "out_ch": 16,
    "stride": 1
}

# 3 groups, each 3 blocks, each 2 convs
groups_cfg = [
    ("layer1", 16, 16, 1),
    ("layer2", 16, 32, 2),
    ("layer3", 32, 64, 2)
]

for g_name, in_ch_base, out_ch, first_stride in groups_cfg:
    for b in range(3):
        prefix = f"{g_name}_{b}"
        in_ch = in_ch_base if b == 0 else out_ch
        stride1 = first_stride if b == 0 else 1
        print(f"Folding {prefix} (in={in_ch}, out={out_ch}, stride={stride1})...")
        
        # Conv1
        w1, b1 = fold_bn(f"{prefix}_conv1_weight.txt", f"{prefix}_bn1", out_ch, in_ch)
        # Conv2
        w2, b2 = fold_bn(f"{prefix}_conv2_weight.txt", f"{prefix}_bn2", out_ch, out_ch)

        folded_layers[f"{prefix}_conv1"] = {
            "weight": w1.flatten().tolist(),
            "bias": b1.tolist(),
            "in_ch": in_ch,
            "out_ch": out_ch,
            "stride": stride1
        }
        folded_layers[f"{prefix}_conv2"] = {
            "weight": w2.flatten().tolist(),
            "bias": b2.tolist(),
            "in_ch": out_ch,
            "out_ch": out_ch,
            "stride": 1
        }

# FC layer (linear)
fc_w = get_git_file("inputs/checkpoint/linear_weight.txt")
fc_b = get_git_file("inputs/checkpoint/linear_bias.txt")
assert len(fc_w) == 10 * 64
assert len(fc_b) == 10
folded_layers["fc"] = {
    "weight": fc_w,
    "bias": fc_b,
    "in_features": 64,
    "out_features": 10
}

# Save folded model
folded_json_path = fixtures_dir / "resnet20_folded.json"
with open(folded_json_path, "w", encoding="utf-8") as f:
    json.dump(folded_layers, f)
print(f"Saved folded model weights to {folded_json_path}")

# Also save binary weights for C++
folded_bin_path = fixtures_dir / "resnet20_weights.bin"
with open(folded_bin_path, "wb") as f:
    # Stem
    f.write(struct.pack(f"{len(folded_layers['stem']['weight'])}d", *folded_layers['stem']['weight']))
    f.write(struct.pack(f"{len(folded_layers['stem']['bias'])}d", *folded_layers['stem']['bias']))
    for g_name, _, _, _ in groups_cfg:
        for b in range(3):
            prefix = f"{g_name}_{b}"
            w1 = folded_layers[f"{prefix}_conv1"]['weight']
            b1 = folded_layers[f"{prefix}_conv1"]['bias']
            w2 = folded_layers[f"{prefix}_conv2"]['weight']
            b2 = folded_layers[f"{prefix}_conv2"]['bias']
            f.write(struct.pack(f"{len(w1)}d", *w1))
            f.write(struct.pack(f"{len(b1)}d", *b1))
            f.write(struct.pack(f"{len(w2)}d", *w2))
            f.write(struct.pack(f"{len(b2)}d", *b2))
    # FC
    f.write(struct.pack(f"{len(fc_w)}d", *fc_w))
    f.write(struct.pack(f"{len(fc_b)}d", *fc_b))

print(f"Saved {folded_bin_path} ({os.path.getsize(folded_bin_path)} bytes)")
print("ALL TICKET 01 FIXTURES SUCCESSFULLY EXPORTED AND VALIDATED!")
