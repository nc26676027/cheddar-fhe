#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "UserInterface.h"
#include "extension/DiscreteIdentityBootstrap.h"

using namespace cheddar;
using Json = nlohmann::json;

namespace {
Json Read(const char *file) {
  std::ifstream stream(std::string(PARAM_DIR) + "/" + file);
  if (!stream) throw std::runtime_error(std::string("Missing fixture: ") + file);
  return Json::parse(stream);
}
}  // namespace

TEST(DiscreteIdentityBaseline, EndToEndEncryptedBootstrap32768Slots) {
  const auto profile = Read("identity19_meboot.json");
  const auto manifest = Read("discrete_direct5_d47.json");
  auto q = profile.at("q").get<std::vector<uint64_t>>();
  const auto p = profile.at("p").get<std::vector<uint64_t>>();
  // The original Discrete CKKS paper (CCS 2025 Table 2 Direct2Cln19) uses
  // 35-45 bit primes for StC (66 bits across 2 levels), not MEBoot's 27-bit entry prime.
  q[1] = 1099526176769ULL;
  q[2] = 1099529060353ULL;

  std::vector<std::pair<int, int>> levels;
  for (int count = 1; count <= static_cast<int>(q.size()); ++count) {
    levels.emplace_back(count, 0);
  }

  Parameter<uint64_t> parameter(16, std::exp2(40), 2, levels, q, p);
  parameter.SetSparseHammingWeight(30);
  parameter.SetDenseHammingWeight(30);

  auto context = Context<uint64_t>::Create(parameter);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredModUp();
  context->memory_pool_.EnableStatistics();

  UserInterface<uint64_t> client(context);

  std::vector<std::vector<double>> extractor_coefficients;
  for (const auto &polynomial : manifest.at("coefficients")) {
    std::vector<double> coefficients;
    for (const auto &coefficient : polynomial)
      coefficients.push_back(std::stod(coefficient.get<std::string>()));
    extractor_coefficients.push_back(std::move(coefficients));
  }

  DiscreteIdentityBaseline baseline(context, extractor_coefficients);
  EXPECT_EQ(baseline.OutputLevel(), 8);
  EXPECT_EQ(baseline.OutputScale(), std::exp2(40));

  EvkRequest request;
  baseline.AddRequiredRotations(request);
  client.PrepareRotationKey(request);

  constexpr int slots = 32768;
  std::vector<Complex> messages(slots);
  for (int i = 0; i < slots; ++i) {
    double angle = 2.0 * M_PI * i / double(slots);
    messages[i] = Complex(0.2 * std::cos(angle), 0.2 * std::sin(angle));
  }

  Plaintext<uint64_t> plaintext;
  context->encoder_.Encode(plaintext, 2, std::exp2(40), messages);
  Ciphertext<uint64_t> input;
  client.Encrypt(input, plaintext);
  input.SetNumSlots(slots);

  std::vector<Complex> clean_vals, high_vals;
  Ciphertext<uint64_t> output;
  baseline.Evaluate(output, input, client.GetEvkMap(),
                    [&](const char *stage, const Ciphertext<uint64_t> &ct) {
                      std::cout << "Stage [" << stage << "] level="
                                << context->param_.NPToLevel(ct.GetNP())
                                << " logScale=" << std::log2(ct.GetScale())
                                << " slots=" << ct.GetNumSlots() << std::endl;
                      if (std::string(stage) == "low-cts") {
                        Plaintext<uint64_t> dec;
                        client.Decrypt(dec, ct);
                        std::vector<Complex> vals;
                        context->encoder_.Decode(vals, dec);
                        double max_re_frac = 0, max_im_frac = 0;
                        int bad_re_slot = -1, bad_im_slot = -1;
                        for (int i = 0; i < slots; ++i) {
                          double rf = std::abs(vals[i].real() - std::round(vals[i].real()));
                          double ifr = std::abs(vals[i].imag() - std::round(vals[i].imag()));
                          if (rf > max_re_frac) { max_re_frac = rf; bad_re_slot = i; }
                          if (ifr > max_im_frac) { max_im_frac = ifr; bad_im_slot = i; }
                        }
                        std::cout << "  low-cts max_re_frac=" << max_re_frac << " (slot " << bad_re_slot << ")"
                                  << " max_im_frac=" << max_im_frac << " (slot " << bad_im_slot << ")"
                                  << " certified_radius=" << std::exp2(-12) << std::endl;
                        std::cout << "  low-cts sample[0]=" << vals[0] << " sample[1]=" << vals[1] << std::endl;
                      }
                      if (std::string(stage) == "clean-integer") {
                        Plaintext<uint64_t> dec;
                        client.Decrypt(dec, ct);
                        context->encoder_.Decode(clean_vals, dec);
                        double max_int_err = 0;
                        int bad_int_slot = -1;
                        for (int i = 0; i < slots; ++i) {
                          double err = std::abs(clean_vals[i] - Complex(std::round(clean_vals[i].real()), std::round(clean_vals[i].imag())));
                          if (err > max_int_err) { max_int_err = err; bad_int_slot = i; }
                        }
                        std::cout << "  clean-integer max_int_residual=" << max_int_err
                                  << " (slot " << bad_int_slot << ") value=" << clean_vals[bad_int_slot] << std::endl;
                      }
                      if (std::string(stage) == "accurate-bypass") {
                        Plaintext<uint64_t> dec;
                        client.Decrypt(dec, ct);
                        context->encoder_.Decode(high_vals, dec);
                      }
                    });

  if (!clean_vals.empty() && !high_vals.empty()) {
    double max_high_err = 0;
    int bad_high_slot = -1;
    for (int i = 0; i < slots; ++i) {
      Complex expected_high = Complex(std::round(clean_vals[i].real()), std::round(clean_vals[i].imag())) + messages[i] / baseline.EffectiveGap();
      double err = std::abs(high_vals[i] - expected_high);
      if (err > max_high_err) {
        max_high_err = err;
        bad_high_slot = i;
      }
    }
    std::cout << "  accurate-bypass vs (clean_int + m/g): max_high_err=" << max_high_err
              << " (slot " << bad_high_slot << ") high=" << high_vals[bad_high_slot]
              << " expected_high=" << (Complex(std::round(clean_vals[bad_high_slot].real()), std::round(clean_vals[bad_high_slot].imag())) + messages[bad_high_slot] / baseline.EffectiveGap())
              << " error_scaled_by_g=" << (max_high_err * baseline.EffectiveGap())
              << " bits=" << -std::log2(max_high_err * baseline.EffectiveGap()) << std::endl;
  }

  EXPECT_EQ(output.GetNP(), context->param_.LevelToNP(baseline.OutputLevel()));
  EXPECT_EQ(output.GetNumSlots(), slots);
  context->AssertSameScale(output, baseline.OutputScale());

  Plaintext<uint64_t> decrypted;
  client.Decrypt(decrypted, output);
  std::vector<Complex> results;
  context->encoder_.Decode(results, decrypted);

  std::vector<double> errors(slots);
  double sum_error = 0;
  double max_error = 0;
  size_t worst_slot = 0;
  for (int i = 0; i < slots; ++i) {
    double err = std::abs(results[i] - messages[i]);
    errors[i] = err;
    sum_error += err;
    if (err > max_error) {
      max_error = err;
      worst_slot = i;
    }
  }

  std::vector<double> sorted_errors = errors;
  std::sort(sorted_errors.begin(), sorted_errors.end());
  double avg_error = sum_error / slots;
  double median_error = sorted_errors[slots / 2];
  double p90_error = sorted_errors[static_cast<size_t>(slots * 0.90)];
  double p99_error = sorted_errors[static_cast<size_t>(slots * 0.99)];

  std::cout << "Slot 0 actual=" << results[0] << " expected=" << messages[0]
            << " error=" << errors[0] << " bits=" << -std::log2(errors[0]) << std::endl;
  std::cout << "Slot 1 actual=" << results[1] << " expected=" << messages[1]
            << " error=" << errors[1] << " bits=" << -std::log2(errors[1]) << std::endl;
  std::cout << "Worst slot=" << worst_slot << " actual=" << results[worst_slot]
            << " expected=" << messages[worst_slot]
            << " max_error=" << max_error << " bits=" << -std::log2(max_error) << std::endl;

  std::cout << "Bootstrap Error Distribution across 32768 slots:" << std::endl
            << "  Average: error=" << avg_error << " bits=" << -std::log2(avg_error) << std::endl
            << "  Median:  error=" << median_error << " bits=" << -std::log2(median_error) << std::endl
            << "  P90:     error=" << p90_error << " bits=" << -std::log2(p90_error) << std::endl
            << "  P99:     error=" << p99_error << " bits=" << -std::log2(p99_error) << std::endl
            << "  Worst:   error=" << max_error << " bits=" << -std::log2(max_error) << std::endl;

  // The paper reports ~19 bits for Direct2Cln19 under unpublished author constants.
  // In our independent Remez profile across 32768 slots:
  // Median and average precision reach ~15 bits, while worst-case is bounded above 11.0 bits.
  EXPECT_GT(-std::log2(avg_error), 14.0);
  EXPECT_GT(-std::log2(max_error), 11.0);

  const auto timing = baseline.Profile(output, input, client.GetEvkMap());
  EXPECT_EQ(output.GetNP(), context->param_.LevelToNP(8));
  context->AssertSameScale(output, std::exp2(40));
  client.Decrypt(decrypted, output);
  std::vector<Complex> profiled;
  context->encoder_.Decode(profiled, decrypted);
  EXPECT_EQ(profiled, results);
  std::vector<std::string> stages{"stc", "raised", "low-cts"};
  for (const char *channel : {"cleaner-input-real", "cleaner-input-imag"}) {
    stages.push_back(channel);
    stages.push_back(std::string(channel) + "/extract/basis");
    for (int bit = 0; bit < 5; ++bit)
      stages.push_back(std::string(channel) + "/extract/tree/" +
                       std::to_string(bit));
    for (int bit = 0; bit < 5; ++bit)
      stages.push_back(std::string(channel) + "/extract/" +
                       std::to_string(bit) + "/0");
    for (int iteration = 1; iteration <= 2; ++iteration)
      for (int bit = 0; bit < 5; ++bit)
        stages.push_back(std::string(channel) + "/clean/" +
                         std::to_string(bit) + "/" + std::to_string(iteration));
    stages.push_back(std::string(channel) + "/combine/-1/0");
  }
  for (const char *stage : {"clean-integer", "accurate-bypass", "output", "ReleaseTemps"})
    stages.push_back(stage);
  ASSERT_EQ(timing.stages.size(), stages.size());
  double sum = 0;
  for (size_t i = 0; i < stages.size(); ++i) {
    EXPECT_EQ(timing.stages[i].name, stages[i]);
    EXPECT_TRUE(std::isfinite(timing.stages[i].gpu_ms));
    EXPECT_GE(timing.stages[i].gpu_ms, 0);
    EXPECT_TRUE(std::isfinite(timing.stages[i].wall_ms));
    EXPECT_GE(timing.stages[i].wall_ms, 0);
    sum += timing.stages[i].gpu_ms;
  }
  EXPECT_NEAR(sum, timing.gpu_ms, 0.01);
  EXPECT_TRUE(std::isfinite(timing.wall_ms));
  EXPECT_GE(timing.wall_ms, 0);
  std::cout << "BASELINE_RESEARCH_PROFILE," << timing.stages.size() << ','
            << timing.gpu_ms << ',' << timing.wall_ms << std::endl;
}
