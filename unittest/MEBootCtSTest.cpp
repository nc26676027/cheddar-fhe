#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Testbed.h"
#include "core/AKSKeyMap.h"
#include "extension/MEBootCtS.h"

using namespace cheddar;

TEST_P(Testbed32, LCRSparseDirectTest) {
  int num_slots = 1 << 15;
  int level = 4;
  auto q_vec =
      context_->param_.GetPrimeVector(context_->param_.LevelToNP(level, 0));
  double lcr_scale = static_cast<double>(q_vec.back());

  MEBootCtS<uint32_t> cts(context_, num_slots, 5);
  StripedMatrix matrix;
  std::vector<Complex> diag0(num_slots, Complex(0.5, 0.0));
  matrix[0] = diag0;

  interface_->PrepareAKSKeys(matrix, level, lcr_scale, /*from_sparse=*/true);
  const auto &aks_map = interface_->GetAKSKeyMap();

  LCROperator<uint32_t> lcr_op(context_, matrix, level, lcr_scale);

  std::vector<Complex> input_msg;
  GenerateRandomMessage(input_msg, num_slots, -1.0, 1.0, true);

  Ciphertext<uint32_t> ct_in =
      CreateContractCiphertext(input_msg, level, num_slots, /*use_sparse_secret=*/true);

  std::vector<Complex> expected_msg(num_slots, Complex(0, 0));
  for (const auto &[rot, diag] : matrix) {
    for (int j = 0; j < num_slots; ++j) {
      int idx = (j + rot) % num_slots;
      if (idx < 0) idx += num_slots;
      expected_msg[j] += input_msg[idx] * diag[j];
    }
  }


  Ciphertext<uint32_t> ct_out;
  lcr_op.Evaluate(ct_out, ct_in, aks_map);

  std::vector<Complex> obtained_msg;
  DecryptAndDecode(obtained_msg, ct_out);
  std::cout << "--- LCRSparseDirectTest with 31 diags Check ---" << std::endl;
  CompareMessages(expected_msg, obtained_msg, true, 0.05);
}

TEST_P(Testbed32, MEBootCtSEvaluation) {
  int num_slots = 1 << 15;
  int start_level = 5;

  MEBootCtS<uint32_t> cts(context_, num_slots, start_level, 1.0);

  EXPECT_EQ(cts.GetStartLevel(), start_level);
  EXPECT_EQ(cts.GetEndLevel(), start_level - 3);
  EXPECT_EQ(cts.GetStage2Level(), start_level - 1);

  // 1. Prepare AKS keys for Stage 2 (switching from sparse to dense secret)
  interface_->PrepareAKSKeys(cts.GetStage2Matrix(), cts.GetStage2Level(),
                             cts.GetStage2LCRScale(), /*from_sparse=*/true);
  const auto &aks_map = interface_->GetAKSKeyMap();
  EXPECT_EQ(aks_map.size(), 31);

  // 2. Prepare Galois rotation keys (Stages 3 and 4)
  EvkRequest req;
  cts.AddRequiredRotations(req);
  interface_->PrepareRotationKey(req);

  // 3. Generate random input message and construct ciphertext under sparse contract
  std::vector<Complex> input_msg;
  double amp = (default_scale_ > (1ULL << 35)) ? 0.02 : 1.0;
  GenerateRandomMessage(input_msg, num_slots, -amp, amp, true);

  Ciphertext<uint32_t> ct_in =
      CreateContractCiphertext(input_msg, start_level, num_slots, /*use_sparse_secret=*/true);

  // 4. Compute expected output message by sequentially applying the 4 stages on CPU
  std::vector<Complex> expected_msg = input_msg;
  for (const auto &plan : cts.GetPlans()) {
    std::vector<Complex> next_msg(num_slots, Complex(0, 0));
    for (const auto &[rot, diag] : plan.matrix) {
      for (int j = 0; j < num_slots; ++j) {
        int idx = (j + rot) % num_slots;
        if (idx < 0) idx += num_slots;
        next_msg[j] += expected_msg[idx] * diag[j];
      }
    }
    expected_msg = next_msg;
  }

  // 5. Evaluate MEBootCtS stage by stage
  const auto &plans = cts.GetPlans();

  // Stage 1 (Subring Prefix on sparse secret)
  std::vector<Complex> exp1(num_slots, Complex(0, 0));
  for (const auto &[rot, diag] : plans[0].matrix) {
    for (int j = 0; j < num_slots; ++j) {
      int idx = (j + rot) % num_slots;
      if (idx < 0) idx += num_slots;
      exp1[j] += input_msg[idx] * diag[j];
    }
  }
  Ciphertext<uint32_t> ct_s1;
  cts.EvaluateStage1(ct_s1, ct_in);
  std::vector<Complex> dec1;
  Plaintext<uint32_t> pt1;
  interface_->DecryptSparse(pt1, ct_s1);
  context_->encoder_.Decode(dec1, pt1);
  std::cout << "--- Stage 1 Check ---" << std::endl;
  std::cout << "ct_s1 scale: " << ct_s1.GetScale() << ", level: " << context_->param_.NPToLevel(ct_s1.GetNP()) << std::endl;
  CompareMessages(exp1, dec1, true, 0.05);

  // Stage 2 (LCR + AKS key-switch to main secret)
  std::vector<Complex> exp2(num_slots, Complex(0, 0));
  for (const auto &[rot, diag] : plans[1].matrix) {
    for (int j = 0; j < num_slots; ++j) {
      int idx = (j + rot) % num_slots;
      if (idx < 0) idx += num_slots;
      exp2[j] += exp1[idx] * diag[j];
    }
  }

  Ciphertext<uint32_t> ct_s2;
  cts.EvaluateStage2(ct_s2, ct_s1, aks_map);
  std::vector<Complex> dec2;
  DecryptAndDecode(dec2, ct_s2);
  std::cout << "--- Stage 2 Check ---" << std::endl;
  std::cout << "ct_s2 scale: " << ct_s2.GetScale() << ", level: " << context_->param_.NPToLevel(ct_s2.GetNP()) << std::endl;
  CompareMessages(exp2, dec2, true, 0.05);

  // Stage 3
  std::vector<Complex> exp3(num_slots, Complex(0, 0));
  for (const auto &[rot, diag] : plans[2].matrix) {
    for (int j = 0; j < num_slots; ++j) {
      int idx = (j + rot) % num_slots;
      if (idx < 0) idx += num_slots;
      exp3[j] += exp2[idx] * diag[j];
    }
  }
  Ciphertext<uint32_t> ct_s3;
  cts.EvaluateStage3(ct_s3, ct_s2, interface_->GetEvkMap());
  std::vector<Complex> dec3;
  DecryptAndDecode(dec3, ct_s3);
  std::cout << "--- Stage 3 Check ---" << std::endl;
  std::cout << "ct_s3 scale: " << ct_s3.GetScale() << ", level: " << context_->param_.NPToLevel(ct_s3.GetNP()) << std::endl;
  CompareMessages(exp3, dec3, true, 0.05);

  // Stage 4
  Ciphertext<uint32_t> ct_out;
  cts.EvaluateStage4(ct_out, ct_s3, interface_->GetEvkMap());

  // 6. Verify level conservation: start_level (5) -> start_level - 3 (2)
  int out_level = context_->param_.NPToLevel(ct_out.GetNP());
  EXPECT_EQ(out_level, start_level - 3);

  // 7. Decrypt ct_out and verify slot accuracy
  std::vector<Complex> obtained_msg;
  DecryptAndDecode(obtained_msg, ct_out);
  CompareMessages(expected_msg, obtained_msg, true, 0.05);

  // 8. Test SplitRealImag
  Ciphertext<uint32_t> ct_real, ct_imag;
  cts.SplitRealImag(ct_real, ct_imag, ct_out,
                    interface_->GetEvkMap().GetConjugationKey());

  EXPECT_EQ(context_->param_.NPToLevel(ct_real.GetNP()), start_level - 3);
  EXPECT_EQ(context_->param_.NPToLevel(ct_imag.GetNP()), start_level - 3);

  std::vector<Complex> obtained_real;
  DecryptAndDecode(obtained_real, ct_real);
  std::vector<Complex> expected_real(num_slots);
  for (int j = 0; j < num_slots; ++j) {
    expected_real[j] = 2.0 * expected_msg[j].real();
  }
  CompareMessages(expected_real, obtained_real, false, 0.05);

  std::vector<Complex> obtained_imag;
  DecryptAndDecode(obtained_imag, ct_imag);
  std::vector<Complex> expected_imag(num_slots);
  for (int j = 0; j < num_slots; ++j) {
    expected_imag[j] = 2.0 * expected_msg[j].imag();
  }
  CompareMessages(expected_imag, obtained_imag, false, 0.05);
}

TEST_P(Testbed64, MEBootCtSEvaluation) {
  int num_slots = 1 << 15;
  int start_level = 5;

  MEBootCtS<uint64_t> cts(context_, num_slots, start_level, 1.0);

  EXPECT_EQ(cts.GetStartLevel(), start_level);
  EXPECT_EQ(cts.GetEndLevel(), start_level - 3);
  EXPECT_EQ(cts.GetStage2Level(), start_level - 1);

  // 1. Prepare AKS keys for Stage 2 (switching from sparse to dense secret)
  interface_->PrepareAKSKeys(cts.GetStage2Matrix(), cts.GetStage2Level(),
                             cts.GetStage2LCRScale(), /*from_sparse=*/true);
  const auto &aks_map = interface_->GetAKSKeyMap();
  EXPECT_EQ(aks_map.size(), 31);

  // 2. Prepare Galois rotation keys (Stages 3 and 4)
  EvkRequest req;
  cts.AddRequiredRotations(req);
  interface_->PrepareRotationKey(req);

  // 3. Generate random input message and construct ciphertext under sparse contract
  std::vector<Complex> input_msg;
  GenerateRandomMessage(input_msg, num_slots, -1.0, 1.0, true);

  Ciphertext<uint64_t> ct_in =
      CreateContractCiphertext(input_msg, start_level, num_slots, /*use_sparse_secret=*/true);

  // 4. Compute expected output message by sequentially applying the 4 stages on CPU
  std::vector<Complex> expected_msg = input_msg;
  for (const auto &plan : cts.GetPlans()) {
    std::vector<Complex> next_msg(num_slots, Complex(0, 0));
    for (const auto &[rot, diag] : plan.matrix) {
      for (int j = 0; j < num_slots; ++j) {
        int idx = (j + rot) % num_slots;
        if (idx < 0) idx += num_slots;
        next_msg[j] += expected_msg[idx] * diag[j];
      }
    }
    expected_msg = next_msg;
  }

  // 5. Evaluate MEBootCtS
  Ciphertext<uint64_t> ct_out;
  cts.Evaluate(ct_out, ct_in, interface_->GetEvkMap(), aks_map);

  // 6. Verify level conservation: start_level (5) -> start_level - 3 (2)
  int out_level = context_->param_.NPToLevel(ct_out.GetNP());
  EXPECT_EQ(out_level, start_level - 3);

  // 7. Decrypt ct_out and verify slot accuracy
  std::vector<Complex> obtained_msg;
  DecryptAndDecode(obtained_msg, ct_out);
  CompareMessages(expected_msg, obtained_msg, true, 0.05);

  // 8. Test SplitRealImag
  Ciphertext<uint64_t> ct_real, ct_imag;
  cts.SplitRealImag(ct_real, ct_imag, ct_out,
                    interface_->GetEvkMap().GetConjugationKey());

  EXPECT_EQ(context_->param_.NPToLevel(ct_real.GetNP()), start_level - 3);
  EXPECT_EQ(context_->param_.NPToLevel(ct_imag.GetNP()), start_level - 3);

  std::vector<Complex> obtained_real;
  DecryptAndDecode(obtained_real, ct_real);
  std::vector<Complex> expected_real(num_slots);
  for (int j = 0; j < num_slots; ++j) {
    expected_real[j] = 2.0 * expected_msg[j].real();
  }
  CompareMessages(expected_real, obtained_real, false, 0.05);

  std::vector<Complex> obtained_imag;
  DecryptAndDecode(obtained_imag, ct_imag);
  std::vector<Complex> expected_imag(num_slots);
  for (int j = 0; j < num_slots; ++j) {
    expected_imag[j] = 2.0 * expected_msg[j].imag();
  }
  CompareMessages(expected_imag, obtained_imag, false, 0.05);
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
