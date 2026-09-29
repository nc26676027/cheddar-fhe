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

  // Set c1 to constant 1 in coefficient domain across all limbs
  HostVector<word> h_c1(num_q * degree, 0);
  for (int i = 0; i < num_q; ++i) {
    h_c1[i * degree] = 1;
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

  // Probe EVK directly in QP
  {
    NPInfo qp_np(input_np.num_main_, input_np.num_ter_, context_->param_.alpha_);
    const auto &evk = aks_map.GetKey(0);
    int degree = context_->param_.degree_;
    int ter_left = context_->param_.GetMaxNumTer() - input_np.num_ter_;
    int main_left = context_->param_.GetMaxNumMain() - input_np.num_main_;
    DeviceVector<uint32_t> s_qp(qp_np.GetNumTotal() * degree);
    cudaMemcpy(s_qp.data(), interface_->MainSecretConstView(ter_left).data(),
               input_np.GetNumQ() * degree * sizeof(uint32_t),
               cudaMemcpyDeviceToDevice);
    cudaMemcpy(s_qp.data() + input_np.GetNumQ() * degree,
               interface_->MainSecretConstView(ter_left).data() +
                   (input_np.GetNumQ() + main_left) * degree,
               context_->param_.alpha_ * degree * sizeof(uint32_t),
               cudaMemcpyDeviceToDevice);

    DeviceVector<uint32_t> ax_s(qp_np.GetNumTotal() * degree);
    std::vector<DvView<uint32_t>> ax_s_view{ax_s.View(context_->param_.alpha_ * degree)};
    context_->elem_handler_.Mult(ax_s_view, qp_np, {evk.AxConstView(0)},
                                 {s_qp.ConstView(context_->param_.alpha_ * degree)});
    DeviceVector<uint32_t> evk_dec(qp_np.GetNumTotal() * degree);
    std::vector<DvView<uint32_t>> evk_dec_view{evk_dec.View(context_->param_.alpha_ * degree)};
    context_->elem_handler_.Add(evk_dec_view, qp_np, {ax_s.ConstView(context_->param_.alpha_ * degree)},
                                {evk.BxConstView(0)});

    DeviceVector<uint32_t> evk_dec_coeff(qp_np.GetNumTotal() * degree);
    auto evk_dec_coeff_view = evk_dec_coeff.View(context_->param_.alpha_ * degree);
    context_->ntt_handler_.INTT(evk_dec_coeff_view, qp_np, evk_dec.ConstView(context_->param_.alpha_ * degree), true);

    HostVector<uint32_t> h_evk_dec(evk_dec.size());
    cudaMemcpy(h_evk_dec.data(), evk_dec_coeff.data(), evk_dec.size() * sizeof(uint32_t), cudaMemcpyDeviceToHost);
    std::cout << "[DEBUG-EVK-DEC] evk.bx + evk.ax * s in QP INTT coeff (first 3 coeffs of 7 Q limbs):" << std::endl;
    for (int l = 0; l < input_np.GetNumQ(); ++l) {
      std::cout << "  limb " << l << " (q=" << q_vec[l] << "): "
                << h_evk_dec[l * degree + 0] << ", "
                << h_evk_dec[l * degree + 1] << ", "
                << h_evk_dec[l * degree + 2] << std::endl;
    }
  }

  // --- Sub-test 1: c0 only (pure plaintext, c1 = 0) ---
  std::cout << "--- Sub-test 1: c0 only (c1 = 0) ---" << std::endl;
  Ciphertext<uint32_t> ct_pt_only;
  ct_pt_only.ModifyNP(input_np);
  ct_pt_only.SetScale(default_scale_);
  ct_pt_only.SetNumSlots(num_slots);
  Plaintext<uint32_t> pt_only;
  pt_only.ModifyNP(input_np);
  context_->encoder_.Encode(pt_only, level, default_scale_, input_msg);
  cudaMemcpy(ct_pt_only.BxView().data(), pt_only.ConstView(0).data(),
             input_np.GetNumQ() * context_->param_.degree_ * sizeof(uint32_t),
             cudaMemcpyDeviceToDevice);
  cudaMemset(ct_pt_only.AxView().data(), 0,
             input_np.GetNumQ() * context_->param_.degree_ * sizeof(uint32_t));

  Ciphertext<uint32_t> ct_out_pt;
  lcr_op.Evaluate(ct_out_pt, ct_pt_only, aks_map);
  std::vector<Complex> dec_pt;
  DecryptAndDecode(dec_pt, ct_out_pt);
  std::cout << "dec_pt first 5: ";
  PrintVector(dec_pt);

  // --- Sub-test 2: c1 only (c0 = 0, c1 = 1) ---
  std::cout << "--- Sub-test 2: c1 only (c0 = 0) ---" << std::endl;
  Ciphertext<uint32_t> ct_c1_only;
  ct_c1_only.ModifyNP(input_np);
  ct_c1_only.SetScale(default_scale_);
  ct_c1_only.SetNumSlots(num_slots);
  cudaMemset(ct_c1_only.BxView().data(), 0,
             input_np.GetNumQ() * context_->param_.degree_ * sizeof(uint32_t));
  cudaMemcpy(ct_c1_only.AxView().data(), ct_in.AxConstView().data(),
             input_np.GetNumQ() * context_->param_.degree_ * sizeof(uint32_t),
             cudaMemcpyDeviceToDevice);

  Ciphertext<uint32_t> ct_out_c1;
  lcr_op.Evaluate(ct_out_c1, ct_c1_only, aks_map);

  // Debug c0 and c1 of ct_out_c1
  {
    DeviceVector<uint32_t> bx_coeff(input_np.GetNumQ() * context_->param_.degree_);
    auto bx_coeff_view = bx_coeff.View(0, 0);
    context_->ntt_handler_.INTT(bx_coeff_view, input_np, ct_out_c1.BxConstView(), true);
    HostVector<uint32_t> h_bx(bx_coeff.size());
    cudaMemcpy(h_bx.data(), bx_coeff.data(), bx_coeff.size() * sizeof(uint32_t), cudaMemcpyDeviceToHost);

    DeviceVector<uint32_t> ax_coeff(input_np.GetNumQ() * context_->param_.degree_);
    auto ax_coeff_view = ax_coeff.View(0, 0);
    context_->ntt_handler_.INTT(ax_coeff_view, input_np, ct_out_c1.AxConstView(), true);
    HostVector<uint32_t> h_ax(ax_coeff.size());
    cudaMemcpy(h_ax.data(), ax_coeff.data(), ax_coeff.size() * sizeof(uint32_t), cudaMemcpyDeviceToHost);

    std::cout << "[DEBUG-AKS-SPLIT] ct_out_c1.Bx INTT (coeff 0, 1, 2):" << std::endl;
    for (int l = 0; l < input_np.GetNumQ(); ++l) {
      std::cout << "  limb " << l << " (q=" << q_vec[l] << "): "
                << h_bx[l * context_->param_.degree_ + 0] << ", "
                << h_bx[l * context_->param_.degree_ + 1] << ", "
                << h_bx[l * context_->param_.degree_ + 2] << std::endl;
    }
    std::cout << "[DEBUG-AKS-SPLIT] ct_out_c1.Ax INTT (coeff 0, 1, 2):" << std::endl;
    for (int l = 0; l < input_np.GetNumQ(); ++l) {
      std::cout << "  limb " << l << " (q=" << q_vec[l] << "): "
                << h_ax[l * context_->param_.degree_ + 0] << ", "
                << h_ax[l * context_->param_.degree_ + 1] << ", "
                << h_ax[l * context_->param_.degree_ + 2] << std::endl;
    }
  }
  Plaintext<uint32_t> ptxt_c1;
  interface_->Decrypt(ptxt_c1, ct_out_c1);
  DeviceVector<uint32_t> ptxt_c1_coeff(input_np.GetNumQ() * context_->param_.degree_);
  auto ptxt_c1_coeff_view = ptxt_c1_coeff.View(0, 0);
  context_->ntt_handler_.INTT(ptxt_c1_coeff_view, input_np, ptxt_c1.ConstView(0), true);
  HostVector<uint32_t> h_coeff(input_np.GetNumQ() * context_->param_.degree_);
  cudaMemcpy(h_coeff.data(), ptxt_c1_coeff.data(),
             h_coeff.size() * sizeof(uint32_t), cudaMemcpyDeviceToHost);
  std::cout << "ptxt_c1 INTT coeff (first 3 coeffs of each limb):" << std::endl;
  for (int l = 0; l < input_np.GetNumQ(); ++l) {
    std::cout << "  limb " << l << " (q=" << q_vec[l] << "): "
              << h_coeff[l * context_->param_.degree_ + 0] << ", "
              << h_coeff[l * context_->param_.degree_ + 1] << ", "
              << h_coeff[l * context_->param_.degree_ + 2] << std::endl;
  }
  std::vector<Complex> dec_c1;
  DecryptAndDecode(dec_c1, ct_out_c1);
  std::cout << "dec_c1 first 5: ";
  PrintVector(dec_c1);

  // Evaluate LCR
  Ciphertext<uint32_t> ct_out;
  lcr_op.Evaluate(ct_out, ct_in, aks_map);

  EXPECT_EQ(context_->param_.NPToLevel(ct_out.GetNP()), level);

  std::vector<Complex> expected_msg(num_slots);
  for (int j = 0; j < num_slots; ++j) {
    expected_msg[j] = input_msg[j] * diag0[j];
  }

  std::vector<Complex> obtained_msg;
  Plaintext<uint32_t> ptxt_out;
  interface_->Decrypt(ptxt_out, ct_out);
  DeviceVector<uint32_t> ptxt_out_coeff(input_np.GetNumQ() * context_->param_.degree_);
  auto ptxt_out_coeff_view = ptxt_out_coeff.View(0, 0);
  context_->ntt_handler_.INTT(ptxt_out_coeff_view, input_np, ptxt_out.ConstView(0), true);
  HostVector<uint32_t> h_out_coeff(input_np.GetNumQ() * context_->param_.degree_);
  cudaMemcpy(h_out_coeff.data(), ptxt_out_coeff.data(),
             h_out_coeff.size() * sizeof(uint32_t), cudaMemcpyDeviceToHost);
  std::cout << "[DEBUG-LCR] ptxt_out INTT coeff (first 5 coeffs of each limb):" << std::endl;
  // Scan all 32768 coefficients for discrepancy across limbs
  {
    int num_discrepant = 0;
    for (int j = 0; j < context_->param_.degree_; ++j) {
      uint64_t q0 = q_vec[0];
      uint64_t q1 = q_vec[1];
      uint64_t v0 = h_out_coeff[0 * context_->param_.degree_ + j];
      uint64_t v1 = h_out_coeff[1 * context_->param_.degree_ + j];
      uint64_t diff = (v1 >= (v0 % q1)) ? (v1 - (v0 % q1)) : (v1 + q1 - (v0 % q1));
      uint64_t inv_q0 = primeutil::InvMod<uint64_t>(q0 % q1, q1);
      uint64_t x1 = (diff * inv_q0) % q1;
      uint64_t val = v0 + x1 * q0;
      uint64_t M = q0 * q1;
      int64_t sval = (val > M / 2) ? static_cast<int64_t>(val - M) : static_cast<int64_t>(val);

      for (int l = 2; l < input_np.GetNumQ(); ++l) {
        uint64_t ql = q_vec[l];
        int64_t expected = sval % static_cast<int64_t>(ql);
        if (expected < 0) expected += ql;
        uint64_t actual = h_out_coeff[l * context_->param_.degree_ + j];
        if (static_cast<uint64_t>(expected) != actual) {
          if (num_discrepant < 10) {
            std::cout << "[DISCREPANCY] coeff " << j << " limb " << l << " (q=" << ql << "): "
                      << "expected=" << expected << ", actual=" << actual
                      << ", diff=" << ((actual >= expected) ? (actual - expected) : (actual + ql - expected))
                      << std::endl;
          }
          num_discrepant++;
          break;
        }
      }
    }
    std::cout << "[DISCREPANCY TOTAL] " << num_discrepant << " / " << context_->param_.degree_ << std::endl;
  }
  context_->encoder_.Decode(obtained_msg, ptxt_out);
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
