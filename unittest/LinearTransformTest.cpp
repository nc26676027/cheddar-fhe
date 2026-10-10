#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Testbed.h"
#include "extension/LinearTransform.h"
#include "extension/MEBootMatrixPlanner.h"

using namespace cheddar;

namespace {

std::vector<Complex> EvaluatePlainMatrixVector(
    const StripedMatrix &matrix, const std::vector<Complex> &input) {
  int num_slots = static_cast<int>(input.size());
  std::vector<Complex> expected(num_slots, Complex(0.0, 0.0));
  for (const auto &[rot, diag] : matrix) {
    for (int j = 0; j < num_slots; ++j) {
      int idx = (j + rot) % num_slots;
      if (idx < 0) idx += num_slots;
      expected[j] += input[idx] * diag[j];
    }
  }
  return expected;
}

}  // namespace

TEST_P(Testbed32, AutomaticBSGSLinearTransform) {
  int num_slots = 1 << 10;
  int level = 4;
  double pt_scale = context_->param_.GetRescalePrimeProd(level);

  StripedMatrix matrix(num_slots, num_slots);
  std::vector<int> shifts = {0, 4, 8, 12, 16, 20, 24, 28};
  for (int rot : shifts) {
    std::vector<Complex> diag;
    GenerateRandomMessage(diag, num_slots, -0.5, 0.5, true);
    matrix[rot] = std::move(diag);
  }

  // Construct LinearTransform with automatic BSGS deduction
  LinearTransform<uint32_t> lt(context_, matrix, level, pt_scale);
  EXPECT_TRUE(lt.IsUsingBSGS());
  EXPECT_GT(lt.GetBS(), 1);
  EXPECT_GT(lt.GetGS(), 1);

  EvkRequest req;
  lt.AddRequiredRotations(req);
  interface_->PrepareRotationKey(req);

  std::vector<Complex> input_msg;
  GenerateRandomMessage(input_msg, num_slots, -1.0, 1.0, true);
  Ciphertext<uint32_t> ct_in;
  EncodeAndEncrypt(ct_in, input_msg, level);
  ct_in.SetNumSlots(num_slots);

  Ciphertext<uint32_t> ct_out;
  lt.Evaluate(ct_out, ct_in, interface_->GetEvkMap());

  EXPECT_EQ(context_->param_.NPToLevel(ct_out.GetNP()), level - 1);
  EXPECT_EQ(ct_out.GetNumSlots(), num_slots);

  std::vector<Complex> expected = EvaluatePlainMatrixVector(matrix, input_msg);
  std::vector<Complex> obtained;
  DecryptAndDecode(obtained, ct_out);
  CompareMessages(expected, obtained, false, 1e-3);
}

TEST_P(Testbed32, ExplicitBSGSAndMinKSLinearTransform) {
  int num_slots = 1 << 10;
  int level = 4;
  double pt_scale = context_->param_.GetRescalePrimeProd(level);

  StripedMatrix matrix(num_slots, num_slots);
  for (int k = 0; k < 8; ++k) {
    int rot = (k * 8) % num_slots;
    std::vector<Complex> diag;
    GenerateRandomMessage(diag, num_slots, -0.5, 0.5, true);
    matrix[rot] = std::move(diag);
  }

  LinearTransform<uint32_t> lt(context_, matrix, level, pt_scale, /*bs=*/4,
                               /*gs=*/2);

  EvkRequest req;
  lt.AddRequiredRotations(req, /*min_ks=*/false);
  lt.AddRequiredRotations(req, /*min_ks=*/true);
  interface_->PrepareRotationKey(req);

  std::vector<Complex> input_msg;
  GenerateRandomMessage(input_msg, num_slots, -1.0, 1.0, true);
  Ciphertext<uint32_t> ct_in;
  EncodeAndEncrypt(ct_in, input_msg, level);
  ct_in.SetNumSlots(num_slots);

  std::vector<Complex> expected = EvaluatePlainMatrixVector(matrix, input_msg);

  Ciphertext<uint32_t> ct_basic;
  lt.Evaluate(ct_basic, ct_in, interface_->GetEvkMap(), /*min_ks=*/false);
  std::vector<Complex> dec_basic;
  DecryptAndDecode(dec_basic, ct_basic);
  CompareMessages(expected, dec_basic, false, 1e-3);

  Ciphertext<uint32_t> ct_minks;
  lt.Evaluate(ct_minks, ct_in, interface_->GetEvkMap(), /*min_ks=*/true);
  std::vector<Complex> dec_minks;
  DecryptAndDecode(dec_minks, ct_minks);
  CompareMessages(expected, dec_minks, false, 1e-3);
}

TEST_P(Testbed64, DenseFoldedLinearTransform) {
  int num_slots = 1 << 15;
  int level = 3;
  double pt_scale = context_->param_.GetRescalePrimeProd(level);

  auto plans = MEBootMatrixPlanner::PlanCtS(context_, num_slots);
  ASSERT_EQ(plans.size(), 4u);
  const auto &stage4_plan = plans[3];

  LinearTransform<uint64_t> lt(context_, stage4_plan.matrix, level, pt_scale,
                               /*dense_base=*/8);

  EvkRequest req;
  lt.AddRequiredRotations(req);
  interface_->PrepareRotationKey(req);

  std::vector<Complex> input_msg;
  GenerateRandomMessage(input_msg, num_slots, -1.0, 1.0, true);
  Ciphertext<uint64_t> ct_in;
  EncodeAndEncrypt(ct_in, input_msg, level);
  ct_in.SetNumSlots(num_slots);

  Ciphertext<uint64_t> ct_out;
  lt.Evaluate(ct_out, ct_in, interface_->GetEvkMap());

  EXPECT_EQ(context_->param_.NPToLevel(ct_out.GetNP()), level - 1);

  std::vector<Complex> expected =
      EvaluatePlainMatrixVector(stage4_plan.matrix, input_msg);
  std::vector<Complex> obtained;
  DecryptAndDecode(obtained, ct_out);
  CompareMessages(expected, obtained, false, 1e-4);
}

INSTANTIATE_TEST_SUITE_P(
    Cheddar, Testbed32, testing::Values("bootparam_40.json"),
    [](const testing::TestParamInfo<Testbed32::ParamType> &info) {
      std::string param_name = info.param;
      std::replace(param_name.begin(), param_name.end(), '.', '_');
      return param_name;
    });

INSTANTIATE_TEST_SUITE_P(
    Cheddar, Testbed64, testing::Values("bootparam_40_64bit.json"),
    [](const testing::TestParamInfo<Testbed64::ParamType> &info) {
      std::string param_name = info.param;
      std::replace(param_name.begin(), param_name.end(), '.', '_');
      return param_name;
    });
