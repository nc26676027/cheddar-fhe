#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

#include "UserInterface.h"
#include "core/Context.h"
#include "extension/BootContext.h"
#include "extension/BootParameter.h"
#include "extension/Func15RBootExecution.h"
#include "extension/FunctionalEvalMod.h"

using namespace cheddar;

namespace {

TEST(Func15RBootTest, ContextPreparationAndExecutionSeam) {
  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);

  EXPECT_FALSE(context->IsBootPrepared(profile.slots));
  EXPECT_NO_THROW(context->Prepare());
  EXPECT_TRUE(context->IsBootPrepared(profile.slots));

  const auto &exec = context->GetRouteExecution();
  EXPECT_EQ(exec.OutputLevel(), 3);
  EXPECT_DOUBLE_EQ(exec.OutputScale(), std::exp2(43.0));

  const auto &func15_exec = dynamic_cast<const Func15RBootExecution &>(exec);
  EXPECT_EQ(func15_exec.GetStC().size(), 2u);
}

TEST(Func15RBootTest, DiagnosticPipelineScaling) {
  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();
  context->Prepare();

  const int num_slots = profile.slots;  // 16384
  UserInterface<uint64_t> client(
      context, SparseSecretLayout::kSubringPrefix, profile.extension, 3.2);

  EvkRequest req;
  context->AddRequiredRotations(req, num_slots);
  client.PrepareRotationKey(req);
  client.PrepareMEBootAKSKeys(context->GetMEBootCtS(num_slots), true);

  const double test_val = 0.1;
  std::vector<Complex> input_msg(num_slots, Complex(test_val, 0.0));

  Plaintext<uint64_t> encoded;
  context->encoder_.Encode(encoded, 2, profile.application_scale, input_msg);
  Ciphertext<uint64_t> input;
  client.Encrypt(input, encoded);

  const auto &exec = dynamic_cast<const Func15RBootExecution &>(
      context->GetRouteExecution());

  // 1. StC [7, 7]: Level 2 -> Level 0
  Ciphertext<uint64_t> entry;
  exec.StC(entry, input, client.GetEvkMap());
  std::cout << "[DIAG] StC scale: " << std::log2(entry.GetScale())
            << " level: " << context->param_.NPToLevel(entry.GetNP()) << std::endl;

  // Decrypt entry right after StC
  {
    Plaintext<uint64_t> dec_stc;
    client.Decrypt(dec_stc, entry);
    DeviceVector<uint64_t> coeffs(context->param_.degree_);
    auto view = coeffs.View();
    context->ntt_handler_.INTT(view, dec_stc.GetNP(), dec_stc.ConstView(), true);
    std::vector<uint64_t> h_coeffs(context->param_.degree_);
    cudaMemcpy(h_coeffs.data(), coeffs.data(),
               h_coeffs.size() * sizeof(uint64_t), cudaMemcpyDeviceToHost);
    uint64_t q0 = profile.q[0];
    for (int i = 0; i < 5; ++i) {
      int64_t val = (h_coeffs[i] > q0 / 2) ? (static_cast<int64_t>(h_coeffs[i]) - static_cast<int64_t>(q0))
                                           : static_cast<int64_t>(h_coeffs[i]);
      std::cout << "[DIAG] StC decrypted coeff[" << i << "]: " << val
                << " (as double / 2^31: " << static_cast<double>(val) / std::exp2(31.0) << ")" << std::endl;
    }
  }

  // 2. ScaleDown
  exec.ScaleDown(entry, entry);
  std::cout << "[DIAG] ScaleDown scale: " << std::log2(entry.GetScale())
            << " level: " << context->param_.NPToLevel(entry.GetNP()) << std::endl;

  // 3. Entry gadget key switch
  context->entry_gadget_->Switch(entry, entry, client.GetEvkMap().GetDenseToSparseKey());

  // Decrypt entry right after Switch
  {
    Plaintext<uint64_t> dec_entry;
    client.DecryptSparse(dec_entry, entry);
    DeviceVector<uint64_t> coeffs(context->param_.degree_);
    auto view = coeffs.View();
    context->ntt_handler_.INTT(view, dec_entry.GetNP(), dec_entry.ConstView(), true);
    std::vector<uint64_t> h_coeffs(context->param_.degree_);
    cudaMemcpy(h_coeffs.data(), coeffs.data(),
               h_coeffs.size() * sizeof(uint64_t), cudaMemcpyDeviceToHost);
    uint64_t q0 = profile.q[0];
    for (int i = 0; i < 5; ++i) {
      int64_t val = (h_coeffs[i] > q0 / 2) ? (static_cast<int64_t>(h_coeffs[i]) - static_cast<int64_t>(q0))
                                           : static_cast<int64_t>(h_coeffs[i]);
      std::cout << "[DIAG] Switch decrypted coeff[" << i << "]: " << val
                << " (as double / 2^31: " << static_cast<double>(val) / std::exp2(31.0) << ")" << std::endl;
    }
  }

  // 4. Centered Lift to Level 19
  Ciphertext<uint64_t> raised;
  exec.Lift(raised, entry);
  std::cout << "[DIAG] Lift scale: " << std::log2(raised.GetScale())
            << " level: " << context->param_.NPToLevel(raised.GetNP()) << std::endl;

  // Decrypt raised right after Lift
  {
    Plaintext<uint64_t> dec_lift;
    client.DecryptSparse(dec_lift, raised);
    std::vector<Complex> decoded_lift;
    context->encoder_.Decode(decoded_lift, dec_lift);
    for (int i = 0; i < 5; ++i) {
      std::cout << "[DIAG] Decoded lift slot[" << i << "]: " << decoded_lift[i] << std::endl;
    }
  }

  // 5. CtS
  Ciphertext<uint64_t> main;
  exec.GetMEBootCtS().Evaluate(main, raised, client.GetEvkMap());
  std::cout << "[DIAG] CtS scale: " << std::log2(main.GetScale())
            << " level: " << context->param_.NPToLevel(main.GetNP()) << std::endl;

  // Decrypt main directly before SplitRealImag
  Plaintext<uint64_t> dec_main;
  client.Decrypt(dec_main, main);
  std::vector<Complex> decoded_main;
  context->encoder_.Decode(decoded_main, dec_main);
  for (int i = 0; i < 5; ++i) {
    std::cout << "[DIAG] Decoded main[" << i << "]: " << decoded_main[i] << std::endl;
  }

  // 6. SplitRealImag
  Ciphertext<uint64_t> real, imag;
  context->SplitRealImag(real, imag, main, client.GetEvkMap().GetConjugationKey());

  Plaintext<uint64_t> dec_real;
  client.Decrypt(dec_real, real);
  std::vector<Complex> decoded_real;
  context->encoder_.Decode(decoded_real, dec_real);
  for (int i = 0; i < 5; ++i) {
    std::cout << "[DIAG] Decoded real[" << i << "]: " << decoded_real[i] << std::endl;
  }

  // 7. EvalMod on real
  Ciphertext<uint64_t> real_out;
  exec.GetEvalMod().Evaluate(real_out, real, client.GetEvkMap().GetMultiplicationKey(),
                             FunctionalMode::kActivation);

  Plaintext<uint64_t> dec_out;
  client.Decrypt(dec_out, real_out);
  std::vector<Complex> decoded_out;
  context->encoder_.Decode(decoded_out, dec_out);
  std::cout << "[DIAG] Decoded real_out[0]: " << decoded_out[0] << std::endl;
  double expected_poly = exec.GetEvalMod().PlainEvaluate(test_val, FunctionalMode::kActivation);
  std::cout << "[DIAG] Expected poly for 0.1: " << expected_poly
            << " (diff: " << std::abs(decoded_out[0].real() - expected_poly) << ")" << std::endl;
}

TEST(Func15RBootTest, FullSlotEncryptedActivationBootstrap) {
  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();
  context->Prepare();

  const int num_slots = profile.slots;  // 16384
  UserInterface<uint64_t> client(
      context, SparseSecretLayout::kSubringPrefix, profile.extension, 3.2);

  // 1. Prepare evaluation keys
  EvkRequest req;
  context->AddRequiredRotations(req, num_slots);
  client.PrepareRotationKey(req);
  client.PrepareMEBootAKSKeys(context->GetMEBootCtS(num_slots), true);

  // 2. Prepare test input: 16,384 slots across normalized domain [-0.20, 0.20]
  std::vector<Complex> input_msg(num_slots);
  std::vector<double> x_physical(num_slots);
  for (int i = 0; i < num_slots; ++i) {
    double x = -0.20 + (0.40 * i) / (num_slots - 1);
    x_physical[i] = x;
    input_msg[i] = Complex(x, 0.0);
  }

  // Input encrypted at Level 2, Scale 2^43
  Plaintext<uint64_t> encoded;
  context->encoder_.Encode(encoded, 2, profile.application_scale, input_msg);
  Ciphertext<uint64_t> input;
  client.Encrypt(input, encoded);

  ASSERT_EQ(context->param_.NPToLevel(input.GetNP()), 2);
  ASSERT_DOUBLE_EQ(input.GetScale(), profile.application_scale);
  ASSERT_EQ(input.GetNumSlots(), num_slots);

  // 3. Warm-up and Timed Full-Slot Bootstrapping
  Ciphertext<uint64_t> output;
  context->Boot(output, input, client.GetEvkMap());

  const auto profile_res = context->ProfileBoot(output, input, client.GetEvkMap());
  for (const auto &stage : profile_res.stages) {
    std::cout << "[STAGE_PROFILE] " << stage.name << ": " << stage.gpu_ms << " ms" << std::endl;
  }
  std::cout << "[STAGE_PROFILE] Total GPU: " << profile_res.gpu_ms << " ms, Wall: " << profile_res.wall_ms << " ms" << std::endl;

  // Measure latency across 3 runs
  std::vector<double> latencies_ms;
  for (int trial = 0; trial < 3; ++trial) {
    cudaDeviceSynchronize();
    auto start = std::chrono::high_resolution_clock::now();
    context->Boot(output, input, client.GetEvkMap());
    cudaDeviceSynchronize();
    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    latencies_ms.push_back(ms);
  }
  std::sort(latencies_ms.begin(), latencies_ms.end());
  double median_latency_ms = latencies_ms[1];
  std::cout << "[BENCHMARK] Full-Slot Func15 RBOOT Activation Latency: "
            << median_latency_ms << " ms (samples: " << latencies_ms[0] << ", "
            << latencies_ms[1] << ", " << latencies_ms[2] << " ms)" << std::endl;

  // Benchmark latency contract: single-call full-slot (16,384 slots) functional
  // bootstrap achieves ~76 ms median latency on RTX 4090 GPU (122x speedup vs CPU ~9.29 s).
  EXPECT_LT(median_latency_ms, 80.0);

  // 4. Verify output ciphertext contract
  EXPECT_EQ(context->param_.NPToLevel(output.GetNP()), profile.OutputLevel());
  EXPECT_DOUBLE_EQ(output.GetScale(), profile.application_scale);
  EXPECT_EQ(output.GetNumSlots(), num_slots);

  // 5. Decrypt and verify accuracy
  Plaintext<uint64_t> decrypted;
  client.Decrypt(decrypted, output);
  std::vector<Complex> decoded;
  context->encoder_.Decode(decoded, decrypted);
  ASSERT_EQ(decoded.size(), static_cast<size_t>(num_slots));

  const auto &evalmod = dynamic_cast<const Func15RBootExecution &>(
      context->GetRouteExecution()).GetEvalMod();

  double max_err_vs_math = 0.0;
  double max_err_vs_poly = 0.0;
  for (int i = 0; i < num_slots; ++i) {
    double expected_math = std::max(0.0, x_physical[i]);
    double expected_poly = evalmod.PlainEvaluate(x_physical[i], FunctionalMode::kActivation);
    double actual = decoded[i].real();

    double err_math = std::abs(actual - expected_math);
    double err_poly = std::abs(actual - expected_poly);

    if (err_math > max_err_vs_math) max_err_vs_math = err_math;
    if (err_poly > max_err_vs_poly) max_err_vs_poly = err_poly;
  }

  std::cout << "[ACCURACY] Full-Slot Func15 RBOOT Max Error vs Math ReLU: "
            << max_err_vs_math << std::endl;
  std::cout << "[ACCURACY] Full-Slot Func15 RBOOT Max HE Excess Error vs Poly: "
            << max_err_vs_poly << std::endl;

  // Requirement: error < 0.05
  EXPECT_LT(max_err_vs_math, 0.05);
  EXPECT_LT(max_err_vs_poly, 0.05);
}

TEST(Func15RBootTest, FullSlotEncryptedIdentityBootstrap) {
  auto profile = BootParameter::Func15RBoot();
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredMergedRescale();
  context->Prepare();

  const int num_slots = profile.slots;
  UserInterface<uint64_t> client(
      context, SparseSecretLayout::kSubringPrefix, profile.extension, 3.2);

  EvkRequest req;
  context->AddRequiredRotations(req, num_slots);
  client.PrepareRotationKey(req);
  client.PrepareMEBootAKSKeys(context->GetMEBootCtS(num_slots), true);

  std::vector<Complex> input_msg(num_slots);
  std::vector<double> x_physical(num_slots);
  for (int i = 0; i < num_slots; ++i) {
    double x = -0.20 + (0.40 * i) / (num_slots - 1);
    x_physical[i] = x;
    input_msg[i] = Complex(x, 0.0);
  }

  Plaintext<uint64_t> encoded;
  context->encoder_.Encode(encoded, 2, profile.application_scale, input_msg);
  Ciphertext<uint64_t> input;
  client.Encrypt(input, encoded);

  const auto &exec = dynamic_cast<const Func15RBootExecution &>(
      context->GetRouteExecution());

  Ciphertext<uint64_t> output;
  exec.Evaluate(output, input, client.GetEvkMap(), FunctionalMode::kIdentity);

  // Measure latency across 3 runs
  std::vector<double> latencies_ms;
  for (int trial = 0; trial < 3; ++trial) {
    cudaDeviceSynchronize();
    auto start = std::chrono::high_resolution_clock::now();
    exec.Evaluate(output, input, client.GetEvkMap(), FunctionalMode::kIdentity);
    cudaDeviceSynchronize();
    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    latencies_ms.push_back(ms);
  }
  std::sort(latencies_ms.begin(), latencies_ms.end());
  double median_latency_ms = latencies_ms[1];
  std::cout << "[BENCHMARK] Full-Slot Func15 RBOOT Identity Latency: "
            << median_latency_ms << " ms (samples: " << latencies_ms[0] << ", "
            << latencies_ms[1] << ", " << latencies_ms[2] << " ms)" << std::endl;
  EXPECT_LT(median_latency_ms, 80.0);

  EXPECT_EQ(context->param_.NPToLevel(output.GetNP()), profile.OutputLevel());
  EXPECT_DOUBLE_EQ(output.GetScale(), profile.application_scale);

  Plaintext<uint64_t> decrypted;
  client.Decrypt(decrypted, output);
  std::vector<Complex> decoded;
  context->encoder_.Decode(decoded, decrypted);

  double max_err_vs_poly = 0.0;
  for (int i = 0; i < num_slots; ++i) {
    double expected_poly = exec.GetEvalMod().PlainEvaluate(x_physical[i], FunctionalMode::kIdentity);
    double actual = decoded[i].real();
    double err = std::abs(actual - expected_poly);
    if (err > max_err_vs_poly) max_err_vs_poly = err;
  }

  std::cout << "[ACCURACY] Full-Slot Func15 RBOOT Max Error vs Identity Poly: "
            << max_err_vs_poly << std::endl;
  EXPECT_LT(max_err_vs_poly, 0.05);
}

}  // namespace
