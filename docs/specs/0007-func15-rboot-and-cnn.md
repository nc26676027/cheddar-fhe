# Spec: Func15 RBOOT and ResNet-20 CNN GPU Port

**Status:** ready-for-agent
**Date:** 2026-10-21
**Source revision:** 36435fb
**Delivery:** Local markdown specifications and tickets under `docs/specs/` and `.scratch/` (GitHub issues disabled).
**Tickets:** `.scratch/func15-rboot-and-cnn/issues/`

## Problem Statement

Cheddar-FHE currently supports high-performance GPU bootstrapping only for the $\log N = 16$ Identity refresh routes (`Identity19` and `DiscreteCKKSMeBoot`). However, the MEBOOT paper's most impactful application demonstration is **Functional Bootstrapping under $\log N = 15$ (`Func15` RBOOT)** and its evaluation on a deep convolutional neural network (**ResNet-20 on CIFAR-10**). 

In the paper's original Go/Lattigo CPU reference implementation (`paper1-linear-bootstrap`), `Func15` fuses homomorphic bootstrapping with ReLU non-linear activation via a composite minimax arcsine/even-part polynomial, eliminating separate activation evaluation. While this achieved a 4.1× speedup over published RBOOT CPU baselines, single-image inference still requires **~221.7 seconds** on CPU, and individual `Func15` refreshes take **~9.29 seconds**. 

Downstream users cannot currently run functional bootstrapping or neural network inference on GPU in Cheddar-FHE because:
1. `cheddar-fhe` lacks $\log N = 15$ bootstrap parameter definitions, matrix decomposition schedules, and entry gadget bindings.
2. The GPU polynomial evaluation pipeline only supports identity modular reduction ($\cos(2\pi x)$ or integer cleaning), not functional composite activation maps.
3. There is no end-to-end encrypted CNN inference pipeline or NeuJeans-style Conv-StC fusion in Cheddar.

## Solution

Port the $\log N = 15$ `Func15` RBOOT functional bootstrapping route and the ResNet-20 CIFAR-10 inference pipeline from `paper1-linear-bootstrap` to `cheddar-fhe`, accelerating both on CUDA:

1. **Self-Contained `Func15` Parameter & RBOOT Polynomial Assets**:
   Extract and compile the exact Q19/P2 modulus chain, entry gadget primes ($q_0 = 17,179,672,577$, $P_{\text{entry}} = 786,433$), subring configuration ($H = 80$, $N' = 4096$), decomposition widths $[3, 3, 4, 4]$, and certified RBOOT composite polynomial coefficients ($K = 22$, actual degree 62, $DA = 2$, with degree-15 arcsine odd/even-part tails) directly into Cheddar.
2. **GPU `Func15` Functional Bootstrap Route**:
   Expose `BootRoute::kFunc15RBoot` and `BootParameter::Func15RBoot()` through `BootContext`. Generalize `MEBootMatrixPlanner` and `BootTransforms` to support 16,384 slots. Implement GPU `FunctionalEvalMod` that evaluates the composite arcsine-ReLU polynomial during bootstrapping, taking an input ciphertext at Level 2, Scale $2^{43}$ and returning a ReLU-activated ciphertext at Level 3, Scale $2^{43}$.
3. **GPU ResNet-20 Inference Pipeline**:
   Implement CUDA kernels and evaluators for Conv-StC fusion (fusing convolution matrix diagonals with StC linear transform), residual shortcut addition, average pooling, and fully-connected classification head.
4. **End-to-End Verification**:
   Verify single `Func15` functional bootstrapping accuracy (HE excess error $< 0.05$, matching CPU qualification). Verify ResNet-20 inference on CIFAR-10 test image index 0, guaranteeing that output logits match the reference polynomial model (`func15-selected-polynomial.json`) with max absolute error $< 0.01$ and produce the correct Top-1 predicted class.

## User Stories

1. As an FHE application developer, I want to initialize a `Func15` RBOOT bootstrapping context via `BootContext::Create(param, BootRoute::kFunc15RBoot)`, so that I can refresh and activate ciphertexts without manual parameter plumbing.
2. As an FHE application developer, I want `BootContext::Boot` under `kFunc15RBoot` to take a Level 2, Scale $2^{43}$ ciphertext and return a Level 3, Scale $2^{43}$ ciphertext, so that the output adheres to the exact functional contract expected by subsequent network layers.
3. As an FHE application developer, I want `kFunc15RBoot` to directly evaluate the ReLU activation function during bootstrap, so that I do not spend separate multiplicative depth or latency evaluating non-linear activations.
4. As a performance engineer, I want `MEBootMatrixPlanner` to support $\log M = 14$ (16,384 slots) and widths $[3, 3, 4, 4]$, so that baby-step/giant-step key generation and diagonal encoding match the MEBOOT paper.
5. As an FHE researcher, I want the exact 19-prime main modulus chain and 2-prime auxiliary chain of `Func15` to match `paper1-linear-bootstrap` exactly, so that numerical precision and 128-bit lattice security are identical to the published evidence.
6. As a CUDA developer, I want the underlying NTT/INTT transforms to execute efficiently at degree $N = 32,768$ ($\log N = 15$), so that kernel launches utilize maximum shared memory and registers.
7. As a security reviewer, I want the 4-bit base-16 entry gadget to correctly switch from dense key to subring key with $H = 80$ and $N' = 4096$, so that rotation key footprints are minimized.
8. As a CUDA developer, I want `FunctionalEvalMod` on GPU to evaluate the composite minimax arcsine/even-part polynomial using Chebyshev basis generation and Paterson-Stockmeyer tree evaluation, so that polynomial evaluation latency is minimized.
9. As a testing engineer, I want a dedicated `func15_rboot_test` GTest that checks single-call `Func15` against plaintext reference answers, so that correctness regressions are caught immediately.
10. As a performance engineer, I want single-call `Func15` GPU latency to be benchmarked and reported with microsecond resolution, so that we can compare it with the CPU 9.29 s baseline.
11. As a neural network developer, I want a `ResNet20Classifier` GPU module that loads pretrained weights from a standard binary fixture, so that I can run inference on encrypted CIFAR-10 inputs.
12. As a CUDA developer, I want convolution weights to be pre-fused into the initial StC transform (NeuJeans style), so that the first convolutional layer does not incur separate ciphertext-plaintext multiplication overhead.
13. As a neural network developer, I want residual shortcuts to correctly add previous stage outputs to activated ciphertexts, so that residual connections execute homomorphically.
14. As an FHE researcher, I want the network to execute 19 `Func15` activation refreshes and 2 Identity refreshes, so that the execution graph matches the paper's ResNet-20 specification.
15. As a testing engineer, I want end-to-end inference on CIFAR-10 test index 0 to produce logits matching the golden reference polynomial model with error $< 0.01$, so that algorithmic fidelity is guaranteed.
16. As a testing engineer, I want CIFAR-10 test index 0 to correctly predict label 3 (cat), matching the true label and paper evidence.
17. As a performance engineer, I want to measure total GPU wall time and peak memory for single-image inference, demonstrating dramatic acceleration over the CPU baseline (~221.7 s).
18. As a maintainer, I want existing $\log N = 16$ `Identity19` and `DiscreteCKKSMeBoot` tests to continue passing without regressions, so that introducing $\log N = 15$ does not destabilize existing routes.

## Implementation Decisions

### 1. Seam Architecture
- **Bootstrapping Seam**: Extend `BootContext` and `BootstrapExecution` with `BootRoute::kFunc15RBoot`. Callers invoke `BootContext::Create(param, BootParameter::Func15RBoot())` and call `context->Boot(ct)`.
- **Inference Seam**: Introduce `ResNet20Classifier` in `src/extension/cnn/ResNet20.h`, providing:
  - `Prepare(const std::string &checkpoint_dir, const EvkMap &evk)`
  - `Classify(const Ct &input_image, Ct &output_logits)`
- **Matrix & Transform Seam**: Update `MEBootMatrixPlanner` and `LinearTransform` to parameterize slot count $M$ dynamically based on `param.degree_ / 2`, removing hardcoded 32,768 slot assumptions.

### 2. Functional EvalMod & RBOOT Polynomial
- In Identity MEBOOT, EvalMod computes $\cos(2\pi(x - 0.25))$.
- In `Func15` RBOOT, `FunctionalEvalMod` evaluates the composite minimax polynomial approximation of $\text{ReLU}(x)$:
  $$\text{ReLU}(x) \approx P_{\text{even}}(x) + P_{\text{odd}}(x)$$
  with $K = 22$, actual degree 62, $DA = 2$, and degree-15 arcsine odd/even tails.
- Implemented via GPU Paterson-Stockmeyer polynomial batch evaluation in `src/extension/FunctionalEvalMod.cu`, reusing Cheddar's high-performance `ElementWiseHandler` and `MultiCAccum`.

### 3. Modulus Chain & Parameters
- Ring degree: $N = 32,768$ ($\log N = 15$).
- Main modulus chain: 19 primes ($q_0 = 17,179,672,577$, 34-bit; $q_1, q_2 \approx 29$-bit; $q_3 \dots q_{16} \approx 43$-bit; terminal primes).
- Auxiliary modulus chain: 2 primes ($P \approx 59$-bit).
- Entry primes: $q_0 = 17,179,672,577$, $P_{\text{entry}} = 786,433$.

### 4. NeuJeans Conv-StC Fusion & Head
- Fuses convolutional weight matrix with Slots-to-Coeffs diagonal encodings at scale $2^{43}$.
- Average pooling and fully connected layers implemented via baby-step/giant-step diagonal automorphism rotations and planar accumulation.

## Testing Decisions

- **Good Test Criteria**: Tests must verify mathematically observable external behavior:
  1. Ciphertext level, scale, and slot layout.
  2. Numerical error bounds relative to reference polynomial oracle.
  3. Correct classification label prediction.
  Do not test internal CUDA thread layouts or intermediate register allocations.
- **Test Modules**:
  1. `parameter_func15_test`: Validates parameter generation, Q/P chains, and matrix planning supports.
  2. `functional_evalmod_test`: Validates GPU evaluation of the RBOOT composite polynomial against float64 known answers.
  3. `func15_rboot_test`: Validates single full-slot `Func15` functional bootstrap from Level 2 to Level 3.
  4. `resnet20_cnn_test`: Validates end-to-end CIFAR-10 index 0 inference on GPU, checking logit accuracy and predicted label.
- **Prior Art**: Follows patterns established in `meboot_test`, `meboot_discrete_identity_test`, and `bootstrap_contract_test`.

## Out of Scope

- Training or modifying ResNet-20 model weights.
- Evaluating images outside the official CIFAR-10 test set.
- Multi-GPU distributed inference.
- Alternate network architectures (e.g. ResNet-32, ResNet-56).
- Modifying established $\log N = 16$ `Identity19` or `DiscreteCKKSMeBoot` parameters.

## Further Notes

- The primary source of authority for parameters and golden answers is `d:\code space\paper1-linear-bootstrap` (`meboot/functional`, `meboot/cnn`, and `paper/evidence/measurements/selected-bootstrap-and-cnn-20261005/`).
- CPU reference baseline: ~9.29 s for single Func15 refresh, ~221.7 s for ResNet-20 inference. GPU acceleration is expected to achieve $< 20$ ms per Func15 refresh and $< 2$ s per CNN image on RTX 5060 Ti.
