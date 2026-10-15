#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Testbed.h"
#include "core/AKSKeyMap.h"
#include "extension/MEBootCtS.h"

using namespace cheddar;

namespace {

template <typename word>
void CheckFusedPrefix(Testbed<word> &bed) {
  constexpr int slots = 1 << 15;
  // Include the bootstrap-sized Q basis, not just the small unit-test level.
  for (int level : {5, bed.param_->max_level_}) {
    SCOPED_TRACE(level);
    auto context = bed.context_;
    MEBootCtS<word> cts(context, slots, level, 1.0, {4, 4, 3, 4}, {64, 8}, 16);
    auto np = context->param_.LevelToNP(level, 0);
    auto primes = context->param_.GetPrimeVector(np);
    Ciphertext<word> input(np);
    input.SetScale(bed.default_scale_);
    input.SetNumSlots(slots);
    HostVector<word> b(input.bx_.size()), a(input.ax_.size());
    int degree = context->param_.degree_;
    for (size_t i = 0; i < b.size(); ++i) {
      word q = primes[i / degree];
      b[i] = (i % 3 == 0) ? q - 1 : static_cast<word>((i * 104729ULL) % q);
      a[i] = (i % 5 == 0) ? 0 : static_cast<word>((i * 130363ULL + 17) % q);
    }
    CopyHostToDevice(input.bx_, b);
    CopyHostToDevice(input.ax_, a);

    Ciphertext<word> sum;
    bool first = true;
    double scale = context->param_.GetRescalePrimeProd(level);
    for (const auto &[rotation, diagonal] : cts.GetPlans()[0].matrix) {
      Plaintext<word> encoded(np);
      context->encoder_.Encode(encoded, level, scale, diagonal);
      Ciphertext<word> rotated, term;
      context->Permute(rotated, input, rotation);
      context->Mult(term, rotated, encoded);
      if (first) {
        sum = std::move(term);
        first = false;
      } else {
        context->Add(sum, sum, term);
      }
    }
    Ciphertext<word> reference, fused;
    context->Rescale(reference, sum);
    cts.EvaluateStage1(fused, input);
    auto compare = [&](const Ciphertext<word> &actual) {
      EXPECT_EQ(actual.GetNP(), reference.GetNP());
      EXPECT_EQ(actual.GetScale(), reference.GetScale());
      EXPECT_EQ(actual.GetNumSlots(), reference.GetNumSlots());
      HostVector<word> expected_b, expected_a, actual_b, actual_a;
      CopyDeviceToHost(expected_b, reference.bx_);
      CopyDeviceToHost(expected_a, reference.ax_);
      CopyDeviceToHost(actual_b, actual.bx_);
      CopyDeviceToHost(actual_a, actual.ax_);
      EXPECT_TRUE(expected_b == actual_b) << "Bx differs from unfused prefix";
      EXPECT_TRUE(expected_a == actual_a) << "Ax differs from unfused prefix";
    };
    compare(fused);
    cts.EvaluateStage1(input, input);
    compare(input);
  }
}

}  // namespace

TEST_P(SubringTestbed32, FusedPrefixMatchesUnfused) { CheckFusedPrefix(*this); }
TEST_P(SubringTestbed64, FusedPrefixMatchesUnfused) { CheckFusedPrefix(*this); }

TEST_P(SubringTestbed32, Stage1SubringSecretContract) {
  constexpr int num_slots = 1 << 15;
  constexpr int start_level = 5;
  MEBootCtS<uint32_t> cts(context_, num_slots, start_level, 1.0, {4, 4, 3, 4}, {64, 8}, 16);
  EvkRequest req;
  cts.AddRequiredRotations(req);
  interface_->PrepareRotationKey(req);

  std::vector<Complex> input_msg(num_slots);
  for (int j = 0; j < num_slots; ++j) {
    input_msg[j] = Complex(0.1 * std::sin(j * 0.01),
                           0.1 * std::cos(j * 0.03));
  }
  Ciphertext<uint32_t> input = CreateContractCiphertext(
      input_msg, start_level, num_slots, /*use_sparse_secret=*/true);
  Ciphertext<uint32_t> output;
  cts.EvaluateStage1(output, input);

  std::vector<Complex> expected(num_slots, Complex(0));
  for (const auto &[rot, diag] : cts.GetPlans()[0].matrix) {
    for (int j = 0; j < num_slots; ++j) {
      expected[j] += input_msg[(j + rot) % num_slots] * diag[j];
    }
  }
  EXPECT_EQ(context_->param_.NPToLevel(output.GetNP()), start_level - 1);
  std::vector<Complex> obtained;
  Plaintext<uint32_t> plaintext;
  interface_->DecryptSparse(plaintext, output);
  context_->encoder_.Decode(obtained, plaintext);
  CompareMessages(expected, obtained, true, 1e-3);
}

TEST_P(SubringTestbed32, LCRSparseDirectTest) {
  int num_slots = 1 << 15;
  int level = 4;
  auto q_vec =
      context_->param_.GetPrimeVector(context_->param_.LevelToNP(level, 0));
  double lcr_scale = static_cast<double>(q_vec.back());

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

INSTANTIATE_TEST_SUITE_P(
    Cheddar, SubringTestbed32,
    testing::Values("bootparam_30.json", "bootparam_40.json"),
    [](const testing::TestParamInfo<SubringTestbed32::ParamType> &info) {
      std::string param_name = info.param;
      std::replace(param_name.begin(), param_name.end(), '.', '_');
      return param_name;
    });

INSTANTIATE_TEST_SUITE_P(
    Cheddar, SubringTestbed64, testing::Values("bootparam_40_64bit.json"),
    [](const testing::TestParamInfo<SubringTestbed64::ParamType> &info) {
      std::string param_name = info.param;
      std::replace(param_name.begin(), param_name.end(), '.', '_');
      return param_name;
    });
