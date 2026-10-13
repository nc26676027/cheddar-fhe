#include "Testbed.h"

template <typename word>
void CheckStandardSparseCapacity(Testbed<word> &bed) {
  bed.interface_.reset();
  bed.param_->SetSparseHammingWeight(bed.param_->degree_ / 8);
  ASSERT_GT(bed.param_->GetSparseHammingWeight(), bed.param_->degree_ / 16);
  bed.interface_ = std::make_unique<UserInterface<word>>(bed.context_);

  std::vector<Complex> message(1 << 15, Complex(0.125, -0.25));
  Plaintext<word> encoded, decoded;
  bed.context_->encoder_.Encode(encoded, 4, bed.default_scale_, message);
  Ciphertext<word> input;
  bed.interface_->EncryptUnderContract(input, encoded, 1, true);
  bed.interface_->DecryptSparse(decoded, input);
  std::vector<Complex> obtained;
  bed.context_->encoder_.Decode(obtained, decoded);
  bed.CompareMessages(message, obtained, false, 1e-8);

  // Re-exec: a forked process cannot inherit CUDA's initialized runtime.
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  // Reject the unsupported Subring Prefix before secret sampling.
  EXPECT_EXIT(
      { UserInterface<word> rejected(bed.context_, SparseSecretLayout::kSubringPrefix); },
      testing::ExitedWithCode(EXIT_FAILURE), "exceeds subring capacity");
}

TEST_P(Testbed32, StandardSparseCapacity) { CheckStandardSparseCapacity(*this); }
TEST_P(Testbed64, StandardSparseCapacity) { CheckStandardSparseCapacity(*this); }

INSTANTIATE_TEST_SUITE_P(Cheddar, Testbed32, testing::Values("bootparam_40.json"));
INSTANTIATE_TEST_SUITE_P(Cheddar, Testbed64, testing::Values("bootparam_40_64bit.json"));
