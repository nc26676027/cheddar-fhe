#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "UserInterface.h"
#include "extension/BootContext.h"
#include "extension/DiscreteIdentityBootstrap.h"
#include "extension/MEBootMatrixPlanner.h"

using namespace cheddar;

TEST(MEBOOTDiscreteIdentity, FullSlotIdentityAndCommonInteger) {
  const auto profile = BootParameter::Selected(BootRoute::kDiscreteMEBoot);
  auto parameter = profile.MakeParameter();
  const bool remez = std::getenv("DISCRETE_LEGACY_D95") == nullptr;
  std::ifstream stream(std::string(PARAM_DIR) + (remez ?
      "/discrete_direct6_k21_remez_d63.json" : "/discrete_direct6_d95.json"));
  ASSERT_TRUE(stream.good());
  const auto manifest = nlohmann::json::parse(stream);
  if (remez) {
    ASSERT_EQ(manifest.at("degree"), 63);
    ASSERT_EQ(manifest.at("integer_domain"), nlohmann::json({-22, 21}));
    ASSERT_EQ(manifest.at("argument_map").at("center_divisor"), 43);
    ASSERT_EQ(manifest.at("argument_map").at("encoded_affine_scale"), 1032);
    ASSERT_EQ(manifest.at("argument_map").at("center_numerator"), "(2*I+1)");
    ASSERT_EQ(manifest.at("argument_map").at("error_scale"), "2/43");
    ASSERT_EQ(manifest.at("argument_map").at("encoded_affine_value"), "2/43");
  }
  std::vector<std::vector<double>> coefficients;
  for (const auto &row : manifest.at("coefficients")) {
    std::vector<double> values;
    for (const auto &value : row)
      values.push_back(std::stod(value.get<std::string>()));
    coefficients.push_back(std::move(values));
  }
  auto settings = remez ?
      DiscreteIdentityParameters::FromHW40RemezCoefficients(
          coefficients, manifest.at("argument_map").at("center_divisor"),
          manifest.at("argument_map").at("encoded_affine_scale"),
          manifest.at("argument_map").at("encoded_affine_integer")) :
      DiscreteIdentityParameters::FromCoefficients(
      6, coefficients, "meboot-discrete-identity-hw40");
  if (const char *scale = std::getenv("DISCRETE_EXTRACTION_BITS"))
    settings.extraction_scale = std::exp2(std::stoi(scale));
  if (const char *scale = std::getenv("DISCRETE_BASIS_BITS"))
    settings.input_scale = std::exp2(std::stoi(scale));
  if (const char *level = std::getenv("DISCRETE_ALIGNMENT_LEVEL"))
    settings.cleaner_alignment_level = std::stoi(level);
  if (const char *baby = std::getenv("DISCRETE_EXTRACTOR_BABY_STEP"))
    settings.extraction_baby_step = std::stoi(baby);
  if (const char *counts = std::getenv("DISCRETE_CLEANING_RESCALES")) {
    ASSERT_EQ(std::string(counts).size(), 2);
    settings.cleaning_rescales = {counts[0] - '0', counts[1] - '0'};
  }
  auto context = BootContext<uint64_t>::Create(*parameter, profile, settings);
  context->EnableCenteredModUp();
  context->memory_pool_.EnableStatistics();
  context->Prepare();
  const auto &bootstrap = context->GetRouteExecution();
  const double effective_gap =
      double(profile.q[0]) /
      ((1.0 / 64) * std::exp2(33) * profile.ScaleDownMultiplier());
  std::cout << "schedule_alignment_level=" << settings.cleaner_alignment_level
            << " basis_bits=" << std::log2(settings.input_scale)
            << " extraction_bits=" << std::log2(settings.extraction_scale)
            << " output_level=" << bootstrap.OutputLevel()
            << " extractor_baby_step=" << settings.extraction_baby_step << std::endl;
  const double encoded_divisor =
      settings.input_scale / (2 * BootParameter::kModUpScale) *
          (remez ? 2.0 / 43 : 1.0 / 32);
  const bool alignment_rescale =
      encoded_divisor < 1 || encoded_divisor != std::round(encoded_divisor);
  EXPECT_EQ(bootstrap.OutputLevel(),
            settings.cleaner_alignment_level - (remez ? 7 : 8) - alignment_rescale -
                settings.cleaning_rescales[0] - settings.cleaning_rescales[1]);
  EXPECT_EQ(bootstrap.OutputScale(), std::exp2(40));
  std::cout << "effective_gap=" << effective_gap << std::endl;
  UserInterface<uint64_t> client(context);
  EvkRequest request;
  bootstrap.AddRequiredRotations(request);
  client.PrepareRotationKey(request);
  client.PrepareMEBootAKSKeys(bootstrap.GetMEBootCtS());

  constexpr int slots = 32768;
  std::vector<Complex> messages(slots);
  for (int index = 0; index < slots; ++index) {
    messages[index] = Complex((double(index % 33) / 32 - 0.5) / 2,
                             (double((index / 33) % 33) / 32 - 0.5) / 2);
    switch (index % 7) {
      case 0: messages[index] = {0, 0}; break;
      case 1: messages[index] = {0.25, 0}; break;
      case 2: messages[index] = {-0.25, 0}; break;
      case 3: messages[index] = {0, 0.25}; break;
      case 4: messages[index] = {0, -0.25}; break;
      case 5: messages[index] = {0.25, -0.25}; break;
      default: break;
    }
  }
  Plaintext<uint64_t> plaintext;
  context->encoder_.Encode(plaintext, 2, std::exp2(40), messages);
  Ciphertext<uint64_t> input, output;
  client.Encrypt(input, plaintext);
  std::vector<Complex> high, integers;
  auto decode = [&](const Ciphertext<uint64_t> &ciphertext) {
    Plaintext<uint64_t> decoded;
    client.Decrypt(decoded, ciphertext);
    std::vector<Complex> values;
    context->encoder_.Decode(values, decoded);
    return values;
  };
  ConstContextPtr<uint64_t> core = context;
  const auto cts = MEBootMatrixPlanner::GenerateElementaryCtSStages(core, slots);
  auto plain_cts = [&](std::vector<Complex> values) {
    for (const auto &matrix : cts) {
      std::vector<Complex> next(slots);
      for (const auto &[rotation, diagonal] : matrix)
        for (int index = 0; index < slots; ++index)
          next[index] += diagonal[index] *
              values[(index + rotation + slots) % slots];
      values = std::move(next);
    }
    return values;
  };
  std::vector<Complex> lifted_oracle;
  bootstrap.Evaluate(output, input, client.GetEvkMap(), {},
      [&](const char *stage, const Ciphertext<uint64_t> &ciphertext) {
        std::cout << "stage=" << stage
                  << " level=" << parameter->NPToLevel(ciphertext.GetNP())
                  << " log_scale=" << std::log2(ciphertext.GetScale()) << std::endl;
        const std::string name(stage);
        if (name == "entry") {
          Plaintext<uint64_t> decoded;
          client.DecryptSparse(decoded, ciphertext);
          std::vector<Complex> values;
          context->encoder_.Decode(values, decoded);
          values = plain_cts(std::move(values));
          double maximum = 0;
          for (int index = 0; index < slots; ++index)
            maximum = std::max(maximum,
                std::abs(values[index] * (64.0 / slots) - messages[index]));
          std::cout << "entry_message_error=" << maximum << std::endl;
        }
        if (name == "raised-dense") {
          lifted_oracle = plain_cts(decode(ciphertext));
          const double units = double(profile.q[0]) / std::exp2(33);
          for (auto &value : lifted_oracle) value /= slots * units;
        }
        if (name == "high-cts") high = decode(ciphertext);
        if (name == "clean-integer") integers = decode(ciphertext);
        if (name == "cleaner-input-real" || name == "cleaner-input-imag") {
          const auto values = decode(ciphertext);
          double fractional = 0, imaginary = 0;
          double minimum_integer = 0, maximum_integer = 0;
          for (const auto &normalized : values) {
            const auto value = normalized * (remez ? 21.5 : 32.0) - 0.5;
            fractional = std::max(fractional,
                std::abs(value.real() - std::round(value.real())));
            imaginary = std::max(imaginary, std::abs(value.imag()));
            minimum_integer = std::min(minimum_integer, std::round(value.real()));
            maximum_integer = std::max(maximum_integer, std::round(value.real()));
          }
          std::cout << "cleaner_fractional=" << fractional
                    << " imaginary=" << imaginary
                    << " minimum_integer=" << minimum_integer
                    << " maximum_integer=" << maximum_integer << std::endl;
          EXPECT_LE(fractional, std::exp2(-12));
          EXPECT_LE(imaginary, std::exp2(-12));
          EXPECT_GE(minimum_integer, remez ? -21 : -32);
          EXPECT_LE(maximum_integer, remez ? 21 : 31);
        }
      });
  ASSERT_EQ(high.size(), slots);
  ASSERT_EQ(integers.size(), slots);
  double clean_error = 0, bypass_error = 0;
  double transform_error = 0, raised_error = 0;
  for (int index = 0; index < slots; ++index) {
    const Complex integer(std::round(high[index].real()), std::round(high[index].imag()));
    clean_error = std::max(clean_error, std::abs(integers[index] - integer));
    bypass_error = std::max(bypass_error,
        std::abs((high[index] - integer) * effective_gap - messages[index]));
    transform_error = std::max(transform_error,
        std::abs(high[index] - lifted_oracle[index]) * effective_gap);
    raised_error = std::max(raised_error,
        std::abs((lifted_oracle[index] - integer) * effective_gap - messages[index]));
  }
  std::cout << "clean_integer_error=" << clean_error
            << " bypass_message_error=" << bypass_error
            << " transform_message_error=" << transform_error
            << " raised_message_error=" << raised_error << std::endl;
  const auto values = decode(output);
  ASSERT_EQ(values.size(), slots);
  std::vector<double> errors;
  double sum = 0;
  for (int index = 0; index < slots; ++index) {
    const double error = std::abs(values[index] - messages[index]);
    ASSERT_TRUE(std::isfinite(error));
    errors.push_back(error);
    sum += error;
  }
  std::sort(errors.begin(), errors.end());
  std::cout << "average_bits=" << -std::log2(sum / slots)
            << " median_bits=" << -std::log2(errors[slots / 2])
            << " worst_bits=" << -std::log2(errors.back())
            << " max_error=" << errors.back() << std::endl;
  EXPECT_LT(clean_error, std::exp2(-33));
  ASSERT_LT(errors.back(), std::exp2(-18));
  EXPECT_EQ(output.GetNP(), parameter->LevelToNP(bootstrap.OutputLevel()));
  context->AssertSameScale(output, bootstrap.OutputScale());
  Ciphertext<uint64_t> dispatched;
  context->Boot(dispatched, input, client.GetEvkMap());
  EXPECT_EQ(dispatched.GetNP(), parameter->LevelToNP(bootstrap.OutputLevel()));
  context->AssertSameScale(dispatched, bootstrap.OutputScale());
  const int warmups = std::getenv("DISCRETE_WARMUPS") ?
      std::stoi(std::getenv("DISCRETE_WARMUPS")) : 2;
  const int repetitions = std::getenv("DISCRETE_REPETITIONS") ?
      std::stoi(std::getenv("DISCRETE_REPETITIONS")) : 3;
  const int profiles = std::getenv("DISCRETE_PROFILE_REPETITIONS") ?
      std::stoi(std::getenv("DISCRETE_PROFILE_REPETITIONS")) : 1;
  ASSERT_GE(warmups, 0);
  ASSERT_GT(repetitions, 0);
  ASSERT_GT(profiles, 0);
  const auto &keys = client.GetEvkMap();
  for (int index = 0; index < warmups; ++index)
    bootstrap.Evaluate(output, input, keys);
  for (int index = 0; index < profiles; ++index) {
    const auto timing = context->ProfileBoot(output, input, keys);
    std::cout << "BOOT_CONTEXT_PROFILE_TOTAL," << timing.gpu_ms << ','
              << timing.wall_ms << std::endl;
    for (const auto &stage : timing.stages)
      std::cout << "BOOT_CONTEXT_PROFILE_STAGE," << stage.name << ','
                << stage.gpu_ms << ',' << stage.host_submission_ms << std::endl;
  }
  const auto memory = context->memory_pool_.GetStatistics();
  std::cout << "peak_allocated_bytes=" << memory.peak_bytes
            << " live_bytes=" << memory.live_bytes << std::endl;

  auto key_bytes = [](const EvaluationKey<uint64_t> &key) {
    uint64_t bytes = 0;
    for (const auto &part : key.bx_) bytes += part.size() * sizeof(uint64_t);
    for (const auto &part : key.ax_) bytes += part.size() * sizeof(uint64_t);
    return bytes;
  };
  uint64_t evk_bytes = key_bytes(keys.GetMultiplicationKey()) +
                       key_bytes(keys.GetConjugationKey()) +
                       key_bytes(keys.GetDenseToSparseKey()) +
                       key_bytes(keys.GetSparseToDenseKey());
  for (const auto &[rotation, level] : request)
    if (rotation) evk_bytes += key_bytes(keys.GetRotationKey(rotation));
  for (const auto &[rotation, diagonal] : bootstrap.GetMEBootCtS().GetStage2Matrix())
    evk_bytes += key_bytes(keys.GetAKSKey(rotation));
  std::cout << "resident_evk_bytes=" << evk_bytes << std::endl;

  // Prepared keys and transforms are reused; decode stays outside both timers.
  for (int repetition = 0; repetition < repetitions; ++repetition) {
    cudaEvent_t start, stop;
    ASSERT_EQ(cudaEventCreate(&start), cudaSuccess);
    ASSERT_EQ(cudaEventCreate(&stop), cudaSuccess);
    ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
    const auto begin = std::chrono::steady_clock::now();
    ASSERT_EQ(cudaEventRecord(start), cudaSuccess);
    bootstrap.Evaluate(output, input, keys);
    ASSERT_EQ(cudaEventRecord(stop), cudaSuccess);
    ASSERT_EQ(cudaEventSynchronize(stop), cudaSuccess);
    const double wall_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    float gpu_ms = 0;
    ASSERT_EQ(cudaEventElapsedTime(&gpu_ms, start, stop), cudaSuccess);
    ASSERT_EQ(cudaEventDestroy(start), cudaSuccess);
    ASSERT_EQ(cudaEventDestroy(stop), cudaSuccess);
    const auto repeated = decode(output);
    ASSERT_EQ(repeated.size(), messages.size());
    double maximum = 0;
    for (int index = 0; index < slots; ++index) {
      const double error = std::abs(repeated[index] - messages[index]);
      ASSERT_TRUE(std::isfinite(error));
      maximum = std::max(maximum, error);
    }
    std::cout << "DISCRETE_REPEAT," << repetition << ',' << gpu_ms << ','
              << wall_ms << ',' << maximum << ',' << -std::log2(maximum)
              << std::endl;
    ASSERT_LT(maximum, std::exp2(-18));
    EXPECT_EQ(context->memory_pool_.GetStatistics().live_bytes, memory.live_bytes);
  }
  Ciphertext<uint64_t> alias;
  context->Copy(alias, input);
  bootstrap.Evaluate(alias, alias, keys);
  const auto aliased = decode(alias);
  ASSERT_EQ(aliased.size(), messages.size());
  double alias_error = 0;
  for (int index = 0; index < slots; ++index) {
    const double error = std::abs(aliased[index] - messages[index]);
    ASSERT_TRUE(std::isfinite(error));
    alias_error = std::max(alias_error, error);
  }
  EXPECT_LT(alias_error, std::exp2(-18));
  std::cout << "alias_max_error=" << alias_error << std::endl;
}
