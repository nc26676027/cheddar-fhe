#include "Testbed.h"

class LowQ0PEntry64 : public SubringTestbed64 {};

TEST_P(LowQ0PEntry64, ExactDigits) {
  const auto& gadget = *context_->entry_gadget_;
  ASSERT_EQ(gadget.GetDigitCount(), 13);
  uint64_t q = param_->main_primes_[0];
  int n = param_->degree_;
  HostVector<uint64_t> input(n), reconstruction(n, 0);
  for (int i = 0; i < n; ++i) {
    input[i] = (uint64_t(i) * 6364136223846793005ULL) % q;
  }
  input[0] = 0;
  input[1] = 1;
  input[2] = q - 1;
  input[3] = q / 2;
  for (int j = 0; j < 13; ++j) {
    input[4 + 2 * j] = (uint64_t(1) << (4 * j)) - 1;
    input[5 + 2 * j] = uint64_t(1) << (4 * j);
  }
  DeviceVector<uint64_t> coefficients, digit;
  CopyHostToDevice(coefficients, input);
  for (int j = 0; j < 13; ++j) {
    gadget.DecomposeDigit(digit, coefficients, j);
    HostVector<uint64_t> actual;
    CopyDeviceToHost(actual, digit);
    ASSERT_EQ(actual.size(), 2 * n);
    for (int i = 0; i < n; ++i) {
      uint64_t expected = (input[i] >> (4 * j)) & 15;
      ASSERT_EQ(actual[i], expected) << "digit=" << j << " coefficient=" << i;
      ASSERT_EQ(actual[n + i], expected);
      reconstruction[i] |= actual[i] << (4 * j);
    }
  }
  EXPECT_TRUE(reconstruction == input);
  EXPECT_EQ(cudaDeviceSynchronize(), cudaSuccess);
}

TEST_P(LowQ0PEntry64, DenseToSparseRoundTrip) {
  const auto& key = interface_->GetDenseToSparseKey();
  ASSERT_TRUE(key.IsEntryGadget());
  EXPECT_EQ(key.GetBeta(), 13);
  EXPECT_EQ(key.GetEntryQ(), 1125899908022273ULL);
  EXPECT_EQ(key.GetEntryP(), 786433ULL);
  EXPECT_EQ(key.GetNP(), NPInfo(1, 0, 1));
  EXPECT_EQ(param_->GetSparseHammingWeight(), 256);
  std::vector<Complex> message(1 << 15);
  for (size_t j = 0; j < message.size(); ++j) {
    message[j] = Complex(0.7 * std::sin(j * 0.013),
                         0.7 * std::cos(j * 0.017));
  }
  for (int trial = 0; trial < 3; ++trial) {
    Plaintext<uint64_t> encoded, decrypted, before, difference;
    context_->encoder_.Encode(encoded, -1, default_scale_, message);
    Ciphertext<uint64_t> input, output;
    interface_->Encrypt(input, encoded);
    interface_->Decrypt(before, input);
    context_->entry_gadget_->Switch(output, input, key);
    EXPECT_EQ(output.GetNP(), input.GetNP());
    EXPECT_EQ(output.GetScale(), input.GetScale());
    EXPECT_EQ(output.GetNumSlots(), input.GetNumSlots());
    EXPECT_FALSE(output.HasRx());
    interface_->DecryptSparse(decrypted, output);
    std::vector<Complex> obtained;
    Decode(obtained, decrypted);
    std::vector<Complex> original;
    Decode(original, before);
    difference.ModifyNP(input.GetNP());
    std::vector<DvView<uint64_t>> diff_views{difference.View()};
    context_->elem_handler_.Sub(diff_views, input.GetNP(),
                                {decrypted.ConstView()}, {before.ConstView()});
    auto diff_view = difference.View();
    context_->ntt_handler_.INTT(diff_view, input.GetNP(), difference.ConstView());
    HostVector<uint64_t> coefficient_error;
    CopyDeviceToHost(coefficient_error, difference.mx_);
    int64_t max_coefficient_error = 0;
    uint64_t q = param_->main_primes_[0];
    for (uint64_t value : coefficient_error) {
      int64_t centered = value > q / 2 ? int64_t(value) - int64_t(q)
                                      : int64_t(value);
      max_coefficient_error = std::max(max_coefficient_error, std::abs(centered));
    }
    EXPECT_LT(max_coefficient_error, 512);
    // Separate initial encoding/encryption error from the DtS contribution.
    CompareMessages(original, obtained, false, 1e-6);
    double signal = 0, noise = 0, max_error = 0, input_error = 0;
    for (size_t j = 0; j < message.size(); ++j) {
      signal += std::norm(message[j]);
      noise += std::norm(message[j] - obtained[j]);
      max_error = std::max(max_error, std::abs(message[j] - obtained[j]));
      input_error = std::max(input_error, std::abs(message[j] - original[j]));
    }
    EXPECT_GE(10 * std::log10(signal / noise), 140);
    std::cout << std::setprecision(12) << "ENTRY_PRECISION," << trial << ','
              << max_error << ',' << 10 * std::log10(signal / noise) << ','
              << input_error << ',' << max_coefficient_error << '\n';
    Ciphertext<uint64_t> inplace;
    context_->Copy(inplace, input);
    context_->entry_gadget_->Switch(inplace, inplace, key);
    HostVector<uint64_t> b, a, ib, ia;
    CopyDeviceToHost(b, output.bx_);
    CopyDeviceToHost(a, output.ax_);
    CopyDeviceToHost(ib, inplace.bx_);
    CopyDeviceToHost(ia, inplace.ax_);
    EXPECT_TRUE(b == ib);
    EXPECT_TRUE(a == ia);
  }
  EXPECT_EQ(cudaDeviceSynchronize(), cudaSuccess);
}

TEST_P(LowQ0PEntry64, NoiselessKeyRoundingBound) {
  const int n = param_->degree_;
  const uint64_t q = param_->main_primes_[0];
  const NPInfo qnp(1, 0, 0);
  HostVector<uint64_t> dense_coeff(n, 0), sparse_coeff(n, 0);
  HostVector<uint64_t> ax_coeff(n), message_coeff(n);
  for (int i = 0; i < n; ++i) {
    if (i < n / 2) dense_coeff[i] = i % 2 ? q - 1 : 1;
    if (i % 16 == 0 && i / 16 < 256) {
      sparse_coeff[i] = (i / 16) % 2 ? q - 1 : 1;
    }
    ax_coeff[i] = (uint64_t(i) * 6364136223846793005ULL) % q;
    message_coeff[i] = i % 31;
  }
  DeviceVector<uint64_t> dense, sparse, message, product(n), decrypted(n);
  Ciphertext<uint64_t> input(qnp), output;
  auto transform = [&](DeviceVector<uint64_t>& dst,
                       const HostVector<uint64_t>& src) {
    CopyHostToDevice(dst, src);
    auto view = dst.View();
    context_->ntt_handler_.NTT(view, qnp, dst.ConstView(), true);
  };
  transform(dense, dense_coeff);
  transform(sparse, sparse_coeff);
  transform(input.ax_, ax_coeff);
  transform(message, message_coeff);
  std::vector<DvView<uint64_t>> product_view{product.View()};
  context_->elem_handler_.PMult(product_view, qnp,
                                {input.AxConstView()}, dense.ConstView());
  std::vector<DvView<uint64_t>> b_view{input.BxView()};
  context_->elem_handler_.Sub(b_view, qnp, {message.ConstView()},
                              {product.ConstView()});
  EvaluationKey<uint64_t> key(NPInfo(1, 0, 1), 13);
  context_->entry_gadget_->PopulateKey(key, dense.ConstView(),
                                      sparse.ConstView(), 0);
  context_->entry_gadget_->Switch(output, input, key);
  context_->elem_handler_.PMult(product_view, qnp,
                                {output.AxConstView()}, sparse.ConstView());
  std::vector<DvView<uint64_t>> decrypt_view{decrypted.View()};
  context_->elem_handler_.Add(decrypt_view, qnp,
                              {output.BxConstView()}, {product.ConstView()});
  context_->elem_handler_.Sub(decrypt_view, qnp,
                              {decrypted.ConstView()}, {message.ConstView()});
  auto view = decrypted.View();
  context_->ntt_handler_.INTT(view, qnp, decrypted.ConstView());
  HostVector<uint64_t> error;
  CopyDeviceToHost(error, decrypted);
  int64_t maximum = 0;
  for (uint64_t value : error) {
    int64_t centered = value > q / 2 ? int64_t(value) - int64_t(q)
                                    : int64_t(value);
    maximum = std::max(maximum, std::abs(centered));
  }
  // Two centered P residues introduce at most (H+1)/2 coefficient error.
  EXPECT_LE(maximum, 129);
  std::cout << "ENTRY_NOISELESS_MAX_COEFFICIENT," << maximum << '\n';
  EXPECT_EQ(cudaDeviceSynchronize(), cudaSuccess);
}

INSTANTIATE_TEST_SUITE_P(Experiment, LowQ0PEntry64,
    testing::Values("bootparam_40_64bit_lowp_entry.json"));
