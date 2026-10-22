#include <gtest/gtest.h>

#include <cmath>
#include <chrono>
#include <iostream>
#include <vector>

#include "UserInterface.h"
#include "core/Context.h"
#include "extension/BootParameter.h"
#include "extension/FunctionalEvalMod.h"
#include "extension/Func15RBootCoefficients.inc"

using namespace cheddar;

namespace {

TEST(FunctionalEvalModTest, PlainEvaluateMatchesMathematicalSpec) {
  const auto profile = BootParameter::Func15RBoot();
  const auto param = profile.MakeParameter();
  auto context = Context<uint64_t>::Create(*param);

  FunctionalEvalMod evalmod(context, profile);

  // Verify against certified sample model points in normalized domain [-0.25, 0.25]
  EXPECT_NEAR(evalmod.PlainEvaluate(0.0, FunctionalMode::kActivation), 0.0010789, 1e-5);
  EXPECT_NEAR(evalmod.PlainEvaluate(0.0, FunctionalMode::kIdentity), 0.0, 1e-5);

  EXPECT_NEAR(evalmod.PlainEvaluate(0.15, FunctionalMode::kActivation), 0.150511, 1e-4);
  EXPECT_NEAR(evalmod.PlainEvaluate(0.15, FunctionalMode::kIdentity), 0.149887, 1e-4);

  EXPECT_NEAR(evalmod.PlainEvaluate(-0.15, FunctionalMode::kActivation), 0.000624, 1e-4);
  EXPECT_NEAR(evalmod.PlainEvaluate(-0.15, FunctionalMode::kIdentity), -0.149887, 1e-4);

  EXPECT_NEAR(evalmod.PlainEvaluate(0.20, FunctionalMode::kIdentity), 0.196088, 1e-4);
  EXPECT_NEAR(evalmod.PlainEvaluate(-0.20, FunctionalMode::kIdentity), -0.196088, 1e-4);

  EXPECT_NEAR(evalmod.PlainEvaluate(0.25, FunctionalMode::kActivation), 0.23297, 1e-4);
  EXPECT_NEAR(evalmod.PlainEvaluate(-0.25, FunctionalMode::kActivation), 0.01487, 1e-4);
}

TEST(FunctionalEvalModTest, MainChebyshevEncryptedEvaluation) {
  const auto profile = BootParameter::Func15RBoot();
  const auto param = profile.MakeParameter();
  auto context = Context<uint64_t>::Create(*param);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();

  UserInterface<uint64_t> client(
      context, SparseSecretLayout::kSubringPrefix, profile.extension, 3.2);

  const int num_slots = profile.slots;
  const double test_val = 0.1;
  std::vector<Complex> values(num_slots, Complex(test_val, 0.0));

  double target = profile.application_scale;
  for (int i = 0; i < profile.double_angle; ++i) {
    target = std::sqrt(target * profile.q[8 + 1 + i]);
  }
  const double input_scale = std::ldexp(profile.application_scale, profile.basis_lift);

  Plaintext<uint64_t> encoded;
  context->encoder_.Encode(encoded, 16, input_scale, values);

  Ciphertext<uint64_t> input;
  client.Encrypt(input, encoded);

  std::vector<double> coeffs(
      func15_rboot::kMainChebyshevCoeffs,
      func15_rboot::kMainChebyshevCoeffs + func15_rboot::kMainDegree + 1);
  EvalPoly<uint64_t> poly(coeffs, 16, input_scale, target, true,
                          std::numeric_limits<double>::denorm_min());
  poly.Compile(context);

  Ciphertext<uint64_t> out;
  poly.Evaluate(context, out, input, client.GetMultiplicationKey());

  Plaintext<uint64_t> pt_out;
  client.Decrypt(pt_out, out);
  std::vector<Complex> dec_out;
  context->encoder_.Decode(dec_out, pt_out);

  double expected = poly.PlainEvaluate(test_val);
  double error = std::abs(dec_out[0].real() - expected);
  std::cout << "[CHEB_EVAL] Out[0]: " << dec_out[0] << ", Expected: " << expected
            << ", Error: " << error << std::endl;
  EXPECT_LT(error, 1e-4);
}

TEST(FunctionalEvalModTest, EncryptedActivationEvaluation) {
  const auto profile = BootParameter::Func15RBoot();
  const auto param = profile.MakeParameter();
  auto context = Context<uint64_t>::Create(*param);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();

  FunctionalEvalMod evalmod(context, profile);

  UserInterface<uint64_t> client(
      context, SparseSecretLayout::kSubringPrefix, profile.extension, 3.2);

  const int num_slots = profile.slots;  // 16384
  std::vector<Complex> values(num_slots);
  std::vector<double> x_physical(num_slots);
  const double c2s = func15_rboot::kC2S;

  for (int i = 0; i < num_slots; ++i) {
    // Physical domain in [-4.5, 4.5]
    double x = -4.5 + (9.0 * i) / (num_slots - 1);
    x_physical[i] = x;
    // Post-CtS slot values carry x * c2s
    values[i] = Complex(x * c2s, 0.0);
  }

  Plaintext<uint64_t> encoded;
  context->encoder_.Encode(encoded, profile.mod1_level, profile.application_scale, values);

  Ciphertext<uint64_t> input;
  client.Encrypt(input, encoded);

  EXPECT_EQ(context->param_.NPToLevel(input.GetNP()), profile.mod1_level);
  EXPECT_DOUBLE_EQ(input.GetScale(), profile.application_scale);

  Ciphertext<uint64_t> output;
  auto t_start = std::chrono::high_resolution_clock::now();
  evalmod.Evaluate(output, input, client.GetMultiplicationKey(), FunctionalMode::kActivation);
  auto t_end = std::chrono::high_resolution_clock::now();
  double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
  std::cout << "[PROFILE] Encrypted FunctionalEvalMod Activation latency: "
            << elapsed_ms << " ms" << std::endl;

  EXPECT_EQ(context->param_.NPToLevel(output.GetNP()), profile.OutputLevel());
  EXPECT_DOUBLE_EQ(output.GetScale(), profile.application_scale);

  Plaintext<uint64_t> decrypted;
  client.Decrypt(decrypted, output);

  std::vector<Complex> decoded;
  context->encoder_.Decode(decoded, decrypted);
  ASSERT_EQ(decoded.size(), num_slots);

  double max_error = 0.0;
  for (int i = 0; i < num_slots; ++i) {
    double plain_expected = evalmod.PlainEvaluate(x_physical[i], FunctionalMode::kActivation);
    double actual = decoded[i].real();
    double err = std::abs(actual - plain_expected);
    if (err > max_error) max_error = err;
  }

  std::cout << "[PRECISION] Max encrypted activation excess error: " << max_error << std::endl;
  // Ticket 03 spec: HE excess error < 0.05
  EXPECT_LT(max_error, 0.05);
}

TEST(FunctionalEvalModTest, EncryptedIdentityEvaluation) {
  const auto profile = BootParameter::Func15RBoot();
  const auto param = profile.MakeParameter();
  auto context = Context<uint64_t>::Create(*param);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();

  FunctionalEvalMod evalmod(context, profile);

  UserInterface<uint64_t> client(
      context, SparseSecretLayout::kSubringPrefix, profile.extension, 3.2);

  const int num_slots = profile.slots;
  std::vector<Complex> values(num_slots);
  std::vector<double> x_physical(num_slots);
  const double c2s = func15_rboot::kC2S;

  for (int i = 0; i < num_slots; ++i) {
    double x = -4.0 + (8.0 * i) / (num_slots - 1);
    x_physical[i] = x;
    values[i] = Complex(x * c2s, 0.0);
  }

  Plaintext<uint64_t> encoded;
  context->encoder_.Encode(encoded, profile.mod1_level, profile.application_scale, values);

  Ciphertext<uint64_t> input;
  client.Encrypt(input, encoded);

  Ciphertext<uint64_t> output;
  auto t_start = std::chrono::high_resolution_clock::now();
  evalmod.Evaluate(output, input, client.GetMultiplicationKey(), FunctionalMode::kIdentity);
  auto t_end = std::chrono::high_resolution_clock::now();
  double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
  std::cout << "[PROFILE] Encrypted FunctionalEvalMod Identity latency: "
            << elapsed_ms << " ms" << std::endl;

  EXPECT_EQ(context->param_.NPToLevel(output.GetNP()), profile.OutputLevel());
  EXPECT_DOUBLE_EQ(output.GetScale(), profile.application_scale);

  Plaintext<uint64_t> decrypted;
  client.Decrypt(decrypted, output);

  std::vector<Complex> decoded;
  context->encoder_.Decode(decoded, decrypted);
  ASSERT_EQ(decoded.size(), num_slots);

  double max_error = 0.0;
  for (int i = 0; i < num_slots; ++i) {
    double plain_expected = evalmod.PlainEvaluate(x_physical[i], FunctionalMode::kIdentity);
    double actual = decoded[i].real();
    double err = std::abs(actual - plain_expected);
    if (err > max_error) max_error = err;
  }

  std::cout << "[PRECISION] Max encrypted identity excess error: " << max_error << std::endl;
  EXPECT_LT(max_error, 0.05);
}

}  // namespace
