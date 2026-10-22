# 01: Export Func15 and CNN fixtures

**What to build:**
Extract and verify the exact `Func15` parameters (19-prime main modulus chain, 2-prime auxiliary chain, entry primes $q_0 = 17,179,672,577$ and $P_{\text{entry}} = 786,433$, subring $H=80, N'=4096$, widths $[3,3,4,4]$, bases $[64,4]$), certified RBOOT composite polynomial coefficients ($K=22$, actual degree 62, $DA=2$ with degree-15 arcsine odd/even-part tails), CIFAR-10 test image index 0, and ResNet-20 pretrained checkpoint weights from `paper1-linear-bootstrap` into self-contained test fixtures. Provide a host verification script comparing polynomial evaluation against reference outputs.

**Blocked by:** None (can start immediately).

**Status:** completed

- [x] Parameter JSON for `Func15` (`parameters/func15_meboot.json`) exported and SHA256-verified against `paper1-linear-bootstrap`.
- [x] Certified RBOOT composite polynomial coefficients exported into `parameters/func15_rboot_polynomial.json` and inlined into C++ fixture headers (`include/extension/Func15RBootCoefficients.inc`).
- [x] CIFAR-10 test image index 0 (3,072 normalized floats, true label 3) exported into `unittest/fixtures/cifar10_index0.bin` and `.json`.
- [x] ResNet-20 pretrained checkpoint weights and biases exported into `unittest/fixtures/resnet20_weights.bin` and `resnet20_folded.json`.
- [x] Golden reference logits for test image 0 (`func15-selected-polynomial.json`) exported into `unittest/fixtures/resnet20_golden_logits_index0.json`.
- [x] Host verification tool verifies polynomial float64 evaluation against the golden answers across the normalized domain $[-0.25, 0.25]$ (65,537 samples tested; activation max error $4.09 \times 10^{-14} < 10^{-10}$; identity max error $1.05 \times 10^{-14} < 10^{-10}$).
