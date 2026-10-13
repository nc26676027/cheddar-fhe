#include "Testbed.h"

template <typename word>
void CheckExpandedEvalMod(Testbed<word> &bed) {
  auto boot = std::dynamic_pointer_cast<BootContext<word>>(bed.context_);
  boot->PrepareEvalMod();
  int level = boot->boot_param_.GetEvalModStartLevel();
  double start_scale =
      EvalMod<word>::ComputeStartScale(*bed.param_, boot->boot_param_);
  constexpr int slots = 1 << 15;
  std::vector<Complex> message(slots), expected(slots);
  int ratio = 1 << boot->boot_param_.num_double_angle_;
  for (int j = 0; j < slots; ++j) {
    double x = .45 * std::sin(j * .013);
    message[j] = x;
    expected[j] = std::sin(4 * M_PI * ratio * x) / (2 * M_PI);
  }
  Plaintext<word> encoded;
  boot->encoder_.Encode(encoded, level, start_scale, message);
  Ciphertext<word> input, result;
  bed.interface_->Encrypt(input, encoded);
  boot->EvaluateMod(result, input, bed.interface_->GetMultiplicationKey());
  std::vector<Complex> obtained;
  bed.DecryptAndDecode(obtained, result);
  double max_error = 0;
  for (int j = 0; j < slots; ++j)
    max_error = std::max(max_error, std::abs(expected[j] - obtained[j]));
  std::cout << "EVALMOD_ERROR," << sizeof(word) * 8 << ',' << max_error << '\n';
  bed.CompareMessages(expected, obtained, false, sizeof(word) == 4 ? 1e-10 : 1e-8);
}

TEST_P(Testbed32, ExpandedEvalMod) { CheckExpandedEvalMod(*this); }
TEST_P(Testbed64, ExpandedEvalMod) { CheckExpandedEvalMod(*this); }
INSTANTIATE_TEST_SUITE_P(Cheddar, Testbed32, testing::Values("bootparam_40.json"));
INSTANTIATE_TEST_SUITE_P(Cheddar, Testbed64, testing::Values("bootparam_40_64bit.json"));
