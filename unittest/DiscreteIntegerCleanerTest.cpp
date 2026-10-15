#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "UserInterface.h"
#include "extension/DiscreteIntegerCleaner.h"

using namespace cheddar;
using Json = nlohmann::json;

namespace {
Json Read(const char *file) {
  std::ifstream stream(std::string(PARAM_DIR) + "/" + file);
  if (!stream) throw std::runtime_error(std::string("Missing fixture: ") + file);
  return Json::parse(stream);
}
}  // namespace

TEST(DiscreteIntegerCleaner, EncryptedKnownAnswersAndReuse) {
  const auto profile = Read("identity19_meboot.json");
  const auto manifest = Read("discrete_direct5_d47.json");
  const auto answers = Read("discrete-direct5-known-answers.json");
  auto q = profile.at("q").get<std::vector<uint64_t>>();
  const auto p = profile.at("p").get<std::vector<uint64_t>>();
  q.resize(21);
  std::vector<std::pair<int, int>> levels;
  for (int count = 1; count <= 21; ++count) levels.emplace_back(count, 0);
  Parameter<uint64_t> parameter(16, std::exp2(40), 2, levels, q, p);
  // The CCS 2025 direct profile uses the sparse ternary secret itself
  // (h=30); a dense 2048-secret test measures the wrong noise contract.
  parameter.SetSparseHammingWeight(30);
  parameter.SetDenseHammingWeight(30);
  auto context = Context<uint64_t>::Create(parameter);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredModUp();
  context->memory_pool_.EnableStatistics();
  UserInterface<uint64_t> client(context);
  DiscreteCleaningParameters contract;
  contract.input_level = 20;
  contract.input_scale = std::exp2(40);
  contract.extraction_scale = std::exp2(32);
  contract.max_input_error = std::exp2(-12);
  contract.max_digit_error = std::exp2(-9);
  contract.final_rescales = 2;
  for (const auto &polynomial : manifest.at("coefficients")) {
    std::vector<double> coefficients;
    for (const auto &coefficient : polynomial)
      coefficients.push_back(std::stod(coefficient.get<std::string>()));
    contract.coefficients.push_back(std::move(coefficients));
  }
  DiscreteIntegerCleaner cleaner(context, contract);
  const auto &rows = answers.at("rows");
  std::vector<Complex> messages(128);
  for (size_t index = 0; index < messages.size(); ++index)
    messages[index] = std::stod(rows[index % rows.size()].at("input").get<std::string>());
  Plaintext<uint64_t> plaintext;
  context->encoder_.Encode(plaintext, 20, contract.input_scale, messages);
  Ciphertext<uint64_t> input, result;
  client.Encrypt(input, plaintext);
  {
    Plaintext<uint64_t> decoded;
    client.Decrypt(decoded, input);
    std::vector<Complex> values;
    context->encoder_.Decode(values, decoded);
    double maximum = 0;
    for (size_t index = 0; index < values.size(); ++index)
      maximum = std::max(maximum, std::abs(values[index] - messages[index]));
    std::cout << "input_decrypt_error=" << maximum << "\n";
  }
  double max_extract_error = 0, max_clean_error[2]{};
  auto observe = [&](const char *stage, int bit, int iteration,
                     const Ciphertext<uint64_t> &ciphertext) {
    if (bit < 0) return;
    Plaintext<uint64_t> decoded;
    client.Decrypt(decoded, ciphertext);
    std::vector<Complex> values;
    context->encoder_.Decode(values, decoded);
    ASSERT_EQ(values.size(), messages.size());
    double maximum = 0;
    size_t worst_index = 0;
    std::string worst_expected;
    for (size_t index = 0; index < values.size(); ++index) {
      const auto &row = rows[index % rows.size()];
      const auto &expected = iteration == 0 ? row.at("extract")[bit] :
                             row.at("clean")[iteration - 1][bit];
      const double error = std::abs(values[index] -
                                    std::stod(expected.get<std::string>()));
      if (error > maximum) {
        maximum = error;
        worst_index = index;
        worst_expected = expected.get<std::string>();
      }
    }
    std::cout << stage << " digit=" << bit << " iteration=" << iteration
              << " L" << parameter.NPToLevel(ciphertext.GetNP())
              << " logScale=" << std::log2(ciphertext.GetScale())
              << " oracle_error=" << maximum
              << " actual=" << values[worst_index]
              << " expected=" << worst_expected << "\n";
    ASSERT_TRUE(std::isfinite(maximum));
    if (iteration == 0) {
      max_extract_error = std::max(max_extract_error, maximum);
      EXPECT_LT(maximum, std::exp2(-10));
    } else {
      max_clean_error[iteration - 1] = std::max(max_clean_error[iteration - 1], maximum);
      EXPECT_LT(maximum, iteration == 1 ? std::exp2(-16) : std::exp2(-33));
    }
  };
  const auto state = cleaner.Evaluate(result, input, client.GetMultiplicationKey(),
                                      "application", observe);
  EXPECT_EQ(state.extraction_depth, 6);
  EXPECT_EQ(state.cleaning_multiplicative_depth, 4);
  EXPECT_EQ(state.rescale_count, 10);
  EXPECT_EQ(state.level, 10);
  int64_t live_before = 0;
  // Reuse the result allocation and exercise output/input aliasing.
  for (int repetition = 0; repetition < 3; ++repetition) {
    if (repetition == 2) {
      context->Copy(result, input);
      cleaner.Evaluate(result, result, client.GetMultiplicationKey(), "application");
    } else {
      cleaner.Evaluate(result, input, client.GetMultiplicationKey(), "application");
    }
    client.Decrypt(plaintext, result);
    std::vector<Complex> decoded;
    context->encoder_.Decode(decoded, plaintext);
    double maximum = 0;
    for (size_t index = 0; index < decoded.size(); ++index)
      maximum = std::max(maximum, std::abs(decoded[index] -
                   double(rows[index % rows.size()].at("integer").get<int>())));
    std::cout << "combined repetition=" << repetition << " max_integer_error="
              << maximum << " output_bits=" << -std::log2(maximum) << "\n";
    EXPECT_LT(maximum, std::exp2(-30));
    if (repetition == 0)
      live_before = context->memory_pool_.GetStatistics().live_bytes;
    else
      EXPECT_EQ(context->memory_pool_.GetStatistics().live_bytes, live_before);
  }
  const auto memory = context->memory_pool_.GetStatistics();
  EXPECT_EQ(memory.live_bytes, live_before);
  std::cout << "peak_allocated_bytes=" << memory.peak_bytes
            << " live_bytes=" << memory.live_bytes
            << " extraction_oracle_error=" << max_extract_error
            << " clean1_oracle_error=" << max_clean_error[0]
            << " clean2_oracle_error=" << max_clean_error[1] << "\n";
}
