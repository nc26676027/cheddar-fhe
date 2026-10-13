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

template <typename word>
void CheckKeyModulusBoundary(Testbed<word> &bed) {
  const auto &param = *bed.param_;
  auto key_np = param.GetKeySwitchNP(param.max_key_switch_level_);
  EXPECT_EQ(param.max_level_, param.max_key_switch_level_ + 1);
  EXPECT_EQ(bed.interface_->GetMultiplicationKey().GetNP(), key_np);
  EXPECT_EQ(bed.interface_->GetConjugationKey().GetNP(), key_np);
  EXPECT_EQ(bed.interface_->GetSparseToDenseKey().GetNP(), key_np);
  bed.interface_->PrepareRotationKey(1, param.max_key_switch_level_);
  EXPECT_EQ(bed.interface_->GetRotationKey(1).GetNP(), key_np);
  bed.interface_->PrepareRotationKey(2);
  EXPECT_EQ(bed.interface_->GetRotationKey(2).GetNP(), key_np);
  auto over_np = param.LevelToNP(param.max_level_);
  EXPECT_FALSE(over_np.IsSubsetOf(key_np));
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  EXPECT_EXIT({ bed.interface_->PrepareRotationKey(1, param.max_level_); },
              testing::ExitedWithCode(EXIT_FAILURE), "forbidden at OverModRaise");
  EXPECT_EXIT({ param.GetKeySwitchNP(param.max_level_); },
              testing::ExitedWithCode(EXIT_FAILURE), "forbidden at OverModRaise");
  EXPECT_EXIT({
      Ciphertext<word> raised(over_np);
      Ciphertext<word> result;
      bed.context_->MultKey(result, raised, bed.interface_->GetConjugationKey());
    }, testing::ExitedWithCode(EXIT_FAILURE), "forbidden at OverModRaise");
  EXPECT_EXIT({
      bed.param_->SetSparseHammingWeight(32);
      UserInterface<word> rejected(bed.context_, SparseSecretLayout::kSubringPrefix);
    }, testing::ExitedWithCode(EXIT_FAILURE), "qualified sparse weight 256");
  EXPECT_EXIT({
      Plaintext<word> raised(over_np);
      Ciphertext<word> ciphertext;
      bed.interface_->Encrypt(ciphertext, raised);
    }, testing::ExitedWithCode(EXIT_FAILURE), "forbidden at OverModRaise");
#ifdef ENABLE_EXTENSION
  using Diagonals = std::map<int, Plaintext<word>>;
  EXPECT_EXIT({
      std::vector<int> rotations{0};
      Diagonals diagonals;
      bed.interface_->PrepareAKSKeys(rotations, diagonals, param.max_level_, true);
    }, testing::ExitedWithCode(EXIT_FAILURE), "forbidden at OverModRaise");
#endif
}

TEST_P(Testbed32, KeyModulusBoundary) { CheckKeyModulusBoundary(*this); }
TEST_P(Testbed64, KeyModulusBoundary) { CheckKeyModulusBoundary(*this); }

INSTANTIATE_TEST_SUITE_P(Cheddar, Testbed32, testing::Values("bootparam_40.json"));
INSTANTIATE_TEST_SUITE_P(Cheddar, Testbed64, testing::Values("bootparam_40_64bit.json"));
