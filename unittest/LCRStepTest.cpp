#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Testbed.h"
#include "common/PrimeUtils.h"
#include "core/AKSKeyMap.h"
#include "extension/LCROperator.h"
#include "extension/MEBootMatrixPlanner.h"

using namespace cheddar;

namespace {

template <typename word>
Ciphertext<word> CreateLCRContractCiphertext(
    const std::vector<Complex> &input_msg, int level, double default_scale,
    int num_slots, ConstContextPtr<word> context,
    const std::unique_ptr<UserInterface<word>> &interface) {
  NPInfo q_np = context->param_.LevelToNP(level, 0);
  int num_q = q_np.GetNumQ();
  int degree = context->param_.degree_;

  Plaintext<word> pt;
  pt.ModifyNP(q_np);
  context->encoder_.Encode(pt, level, default_scale, input_msg);

  // Set c1 to constant in coefficient domain where CRT extension has v=0
  word c_val = (default_scale > (1ULL << 35) && sizeof(word) == 4) ? 6 : 1;
  HostVector<word> h_c1(num_q * degree, 0);
  for (int i = 0; i < num_q; ++i) {
    h_c1[i * degree] = c_val;
  }
  Ciphertext<word> ct_in;
  ct_in.ModifyNP(q_np);
  ct_in.SetScale(default_scale);
  ct_in.SetNumSlots(num_slots);
  cudaMemcpy(ct_in.AxView().data(), h_c1.data(),
             h_c1.size() * sizeof(word), cudaMemcpyHostToDevice);
  auto ax_view = ct_in.AxView();
  context->ntt_handler_.NTT(ax_view, q_np, ct_in.AxConstView(), true);

  // c0 = pt - c1 * s in R_Q
  int ter_left = context->param_.GetMaxNumTer() - q_np.num_ter_;
  DeviceVector<word> c1_times_s(num_q * degree);
  std::vector<DvView<word>> c1_times_s_view{c1_times_s.View(0)};
  context->elem_handler_.Mult(c1_times_s_view, q_np, {ct_in.AxConstView()},
                              {interface->MainSecretConstView(ter_left)});
  std::vector<DvView<word>> c0_view{ct_in.BxView()};
  context->elem_handler_.Sub(c0_view, q_np, {pt.ConstView(0)},
                             {c1_times_s.ConstView(0)});
  return ct_in;
}

}  // namespace

TEST_P(Testbed32, LCRStage2RotationZero) {
  int num_slots = 1 << 15;
  int level = 4;
  auto q_vec =
      context_->param_.GetPrimeVector(context_->param_.LevelToNP(level, 0));
  double lcr_scale = static_cast<double>(q_vec.back());

  // Create a 1-diagonal matrix with rot = 0
  StripedMatrix mat0;
  std::vector<Complex> diag0(num_slots, Complex(0.5, 0.0));
  mat0[0] = diag0;

  // Prepare AKS key for rot 0
  interface_->PrepareAKSKeys(mat0, level, lcr_scale);
  const auto &aks_map = interface_->GetAKSKeyMap();
  EXPECT_TRUE(aks_map.HasKey(0));

  LCROperator<uint32_t> lcr_op(context_, mat0, level, lcr_scale);

  std::vector<Complex> input_msg;
  GenerateRandomMessage(input_msg, num_slots, true, -1.0, 1.0);

  Ciphertext<uint32_t> ct_in = CreateLCRContractCiphertext<uint32_t>(
      input_msg, level, default_scale_, num_slots, context_, interface_);

  NPInfo input_np = ct_in.GetNP();

  // Check ct_in decryption
  std::vector<Complex> dec_in;
  DecryptAndDecode(dec_in, ct_in);
  CompareMessages(input_msg, dec_in, false, 0.001);

  // Evaluate LCR
  Ciphertext<uint32_t> ct_out;
  lcr_op.Evaluate(ct_out, ct_in, aks_map);

  EXPECT_EQ(context_->param_.NPToLevel(ct_out.GetNP()), level);

  std::vector<Complex> expected_msg(num_slots);
  for (int j = 0; j < num_slots; ++j) {
    expected_msg[j] = input_msg[j] * diag0[j];
  }

  std::vector<Complex> obtained_msg;
  DecryptAndDecode(obtained_msg, ct_out);
  CompareMessages(expected_msg, obtained_msg, true, 0.05);
}

TEST_P(Testbed32, LCRStage2Evaluation) {
  int num_slots = 1 << 15;
  auto plans = MEBootMatrixPlanner::PlanCtS(context_, num_slots);
  ASSERT_EQ(plans.size(), 4);

  const auto &stage2_plan = plans[1];
  EXPECT_EQ(stage2_plan.matrix.GetNumDiag(), 31);

  int level = 4;
  auto q_vec =
      context_->param_.GetPrimeVector(context_->param_.LevelToNP(level, 0));
  double lcr_scale = static_cast<double>(q_vec.back());

  // 1. Prepare AKS keys
  interface_->PrepareAKSKeys(stage2_plan.matrix, level, lcr_scale);
  const auto &aks_map = interface_->GetAKSKeyMap();
  EXPECT_EQ(aks_map.size(), 31);

  // 2. Construct LCROperator
  LCROperator<uint32_t> lcr_op(context_, stage2_plan.matrix, level, lcr_scale);

  // 3. Generate input message and construct ciphertext under LCR contract
  std::vector<Complex> input_msg;
  GenerateRandomMessage(input_msg, num_slots, true, -1.0, 1.0);

  Ciphertext<uint32_t> ct_in = CreateLCRContractCiphertext<uint32_t>(
      input_msg, level, default_scale_, num_slots, context_, interface_);

  NPInfo input_np = ct_in.GetNP();
  EXPECT_EQ(context_->param_.NPToLevel(input_np), level);

  // 4. Evaluate Stage 2
  Ciphertext<uint32_t> ct_out;
  lcr_op.Evaluate(ct_out, ct_in, aks_map);

  // 5. Verify level is conserved
  NPInfo output_np = ct_out.GetNP();
  EXPECT_EQ(context_->param_.NPToLevel(output_np), level);
  EXPECT_EQ(output_np.num_main_, input_np.num_main_);
  EXPECT_EQ(output_np.num_ter_, input_np.num_ter_);
  EXPECT_EQ(output_np.num_aux_, input_np.num_aux_);

  // 6. Compute expected output message: y[j] = sum_rot x[(j + rot) % num_slots] * diag_rot[j]
  std::vector<Complex> expected_msg(num_slots, Complex(0, 0));
  for (const auto &[rot, diag] : stage2_plan.matrix) {
    for (int j = 0; j < num_slots; ++j) {
      int idx = (j + rot) % num_slots;
      if (idx < 0) idx += num_slots;
      expected_msg[j] += input_msg[idx] * diag[j];
    }
  }

  // 7. Decrypt and compare
  std::vector<Complex> obtained_msg;
  DecryptAndDecode(obtained_msg, ct_out);

  CompareMessages(expected_msg, obtained_msg, true, 0.05);
}

TEST_P(Testbed64, LCRStage2Evaluation) {
  int num_slots = 1 << 15;
  auto plans = MEBootMatrixPlanner::PlanCtS(context_, num_slots);
  ASSERT_EQ(plans.size(), 4);

  const auto &stage2_plan = plans[1];
  EXPECT_EQ(stage2_plan.matrix.GetNumDiag(), 31);

  int level = 4;
  auto q_vec =
      context_->param_.GetPrimeVector(context_->param_.LevelToNP(level, 0));
  double lcr_scale = static_cast<double>(q_vec.back());

  // 1. Prepare AKS keys
  interface_->PrepareAKSKeys(stage2_plan.matrix, level, lcr_scale);
  const auto &aks_map = interface_->GetAKSKeyMap();
  EXPECT_EQ(aks_map.size(), 31);

  // 2. Construct LCROperator
  LCROperator<uint64_t> lcr_op(context_, stage2_plan.matrix, level, lcr_scale);

  // 3. Generate input message and construct ciphertext under LCR contract
  std::vector<Complex> input_msg;
  GenerateRandomMessage(input_msg, num_slots, true, -1.0, 1.0);

  Ciphertext<uint64_t> ct_in = CreateLCRContractCiphertext<uint64_t>(
      input_msg, level, default_scale_, num_slots, context_, interface_);

  NPInfo input_np = ct_in.GetNP();
  EXPECT_EQ(context_->param_.NPToLevel(input_np), level);

  // 4. Evaluate Stage 2
  Ciphertext<uint64_t> ct_out;
  lcr_op.Evaluate(ct_out, ct_in, aks_map);

  // 5. Verify level is conserved
  NPInfo output_np = ct_out.GetNP();
  EXPECT_EQ(context_->param_.NPToLevel(output_np), level);
  EXPECT_EQ(output_np.num_main_, input_np.num_main_);
  EXPECT_EQ(output_np.num_ter_, input_np.num_ter_);
  EXPECT_EQ(output_np.num_aux_, input_np.num_aux_);

  // 6. Compute expected output message
  std::vector<Complex> expected_msg(num_slots, Complex(0, 0));
  for (const auto &[rot, diag] : stage2_plan.matrix) {
    for (int j = 0; j < num_slots; ++j) {
      int idx = (j + rot) % num_slots;
      if (idx < 0) idx += num_slots;
      expected_msg[j] += input_msg[idx] * diag[j];
    }
  }

  // 7. Decrypt and compare
  std::vector<Complex> obtained_msg;
  DecryptAndDecode(obtained_msg, ct_out);

  CompareMessages(expected_msg, obtained_msg, true, 0.05);
}

INSTANTIATE_TEST_SUITE_P(
    Cheddar, Testbed32,
    testing::Values("bootparam_30.json", "bootparam_40.json"),
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
