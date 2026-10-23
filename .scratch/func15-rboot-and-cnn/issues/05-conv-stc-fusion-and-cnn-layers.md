# 05: Conv-StC Fusion and CNN Layers

**What to build:**
Implement NeuJeans-style Conv-StC fusion and homomorphic neural network linear layers on GPU. Fuse convolution weight diagonal matrices directly into the initial Slots-to-Coeffs linear transformation so the first convolutional layer requires no separate ciphertext-plaintext multiplication. Implement residual shortcut additions, encrypted average pooling, and fully-connected layer evaluation.

**Blocked by:** 02: LogN15 Parameter and Matrix Planning

**Status:** completed

- [x] Conv-StC fused operator implemented, encoding kernel diagonals directly into StC transform keys at scale $2^{43}$.
- [x] Ciphertext-ciphertext addition with plaintext bias and residual shortcut connections implemented at matching levels and scales.
- [x] Average pooling operator implemented using baby-step/giant-step automorphism rotations and slot averaging.
- [x] Fully-connected classification layer computes 10 class logits from pooled ciphertexts.
- [x] Standalone GTest `cnn_layers_test` passes, verifying that fused Conv-StC, residual addition, and average pooling match float64 plaintext calculations.
