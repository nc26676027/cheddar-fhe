#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "UserInterface.h"
#include "common/PrimeUtils.h"
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

class SignedDiscreteIntegerCleaner : public ::testing::TestWithParam<int> {};

TEST_P(SignedDiscreteIntegerCleaner, EncryptedKnownAnswersAndReuse) {
  const int width = GetParam();
  const bool six_bit = width == 6;
  const auto profile = Read("identity19_meboot.json");
  const auto manifest = Read(six_bit ? "discrete_direct6_d95.json" :
                                     "discrete_direct5_d47.json");
  const auto answers = Read(six_bit ? "discrete-direct6-known-answers.json" :
                                    "discrete-direct5-known-answers.json");
  auto q = profile.at("q").get<std::vector<uint64_t>>();
  const auto p = profile.at("p").get<std::vector<uint64_t>>();
  q.resize(21);
  std::vector<std::pair<int, int>> levels;
  for (int count = 1; count <= 21; ++count) levels.emplace_back(count, 0);
  Parameter<uint64_t> parameter(16, std::exp2(40), 2, levels, q, p);
  // The CCS 2025 direct profile uses the sparse ternary secret itself
  // (h=30); a dense 2048-secret test measures the wrong noise contract.
  // MEBOOT's six-bit cleaner runs under the dense application secret.
  parameter.SetSparseHammingWeight(six_bit ? 2048 : 30);
  parameter.SetDenseHammingWeight(six_bit ? 2048 : 30);
  auto context = Context<uint64_t>::Create(parameter);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredModUp();
  context->memory_pool_.EnableStatistics();
  UserInterface<uint64_t> client(context);
  DiscreteCleaningParameters contract;
  contract.width = width;
  contract.input_level = 20;
  contract.input_scale = std::exp2(40);
  contract.extraction_scale = std::exp2(six_bit ? 43 : 40);
  contract.max_input_error = std::exp2(-12);
  contract.max_digit_error = std::exp2(-9);
  contract.final_rescales = 0;
  for (const auto &polynomial : manifest.at("coefficients")) {
    std::vector<double> coefficients;
    for (const auto &coefficient : polynomial)
      coefficients.push_back(std::stod(coefficient.get<std::string>()));
    contract.coefficients.push_back(std::move(coefficients));
  }
  DiscreteIntegerCleaner cleaner(context, contract);
  const auto &rows = answers.at("rows");
  std::vector<Complex> messages(six_bit ? 256 : 128);
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
  EXPECT_EQ(state.extraction_depth, six_bit ? 7 : 6);
  EXPECT_EQ(state.cleaning_multiplicative_depth, 4);
  EXPECT_EQ(state.rescale_count, six_bit ? 12 : 11);
  EXPECT_EQ(state.level, six_bit ? 8 : 9);
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
    EXPECT_LT(maximum, std::exp2(-29));
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

INSTANTIATE_TEST_SUITE_P(SignedWidths, SignedDiscreteIntegerCleaner,
                        ::testing::Values(5, 6));

TEST(DiscreteIntegerCleaner, AlignedEvalPolyBatch) {
  const auto profile = Read("identity19_meboot.json");
  const auto manifest = Read("discrete_direct5_d47.json");
  const auto answers = Read("discrete-direct5-known-answers.json");
  auto q = profile.at("q").get<std::vector<uint64_t>>();
  const auto p = profile.at("p").get<std::vector<uint64_t>>();
  q.resize(21);
  std::vector<std::pair<int, int>> levels;
  for (int count = 1; count <= 21; ++count) levels.emplace_back(count, 0);
  Parameter<uint64_t> parameter(16, std::exp2(40), 2, levels, q, p);
  parameter.SetSparseHammingWeight(30);
  parameter.SetDenseHammingWeight(30);
  auto context = Context<uint64_t>::Create(parameter);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredModUp();
  UserInterface<uint64_t> client(context);

  std::vector<std::vector<double>> odd;
  for (const auto &polynomial : manifest.at("coefficients")) {
    std::vector<double> coefficients;
    for (const auto &coefficient : polynomial)
      coefficients.push_back(std::stod(coefficient.get<std::string>()));
    coefficients[0] = 0;
    odd.push_back(std::move(coefficients));
  }

  const int input_level = 20;
  const double scale = std::exp2(40);
  EvalPolyBatch<uint64_t> batch(odd, input_level - 1, scale, scale, true);
  batch.Compile(context);

  const auto &rows = answers.at("rows");
  std::vector<Complex> messages(128);
  for (size_t index = 0; index < messages.size(); ++index) {
    messages[index] = std::stod(rows[index % rows.size()].at("input").get<std::string>());
  }
  Plaintext<uint64_t> plaintext;
  context->encoder_.Encode(plaintext, input_level, scale, messages);
  Ciphertext<uint64_t> input;
  client.Encrypt(input, plaintext);

  // 1. Add input_offset (0.5)
  Constant<uint64_t> input_offset;
  context->encoder_.EncodeConstant(input_offset, input_level, scale, 0.5);
  Ciphertext<uint64_t> centered;
  context->Add(centered, input, input_offset);

  // 2. Multiply by 1/16 and rescale
  Constant<uint64_t> divisor_const;
  const double q_top = context->param_.GetRescalePrimeProd(input_level);
  context->encoder_.EncodeConstant(divisor_const, input_level, q_top, 1.0 / 16.0);
  Ciphertext<uint64_t> scaled_arg, argument;
  context->Mult(scaled_arg, centered, divisor_const);
  context->Rescale(argument, scaled_arg);

  // 3. Evaluate polynomial batch
  std::vector<Ciphertext<uint64_t>> digits;
  batch.Evaluate(context, digits, argument, client.GetMultiplicationKey());

  Constant<uint64_t> half_offset;
  context->encoder_.EncodeConstant(half_offset, (input_level - 1) - batch.GetDepth(), scale, 0.5);

  double max_error = 0;
  for (size_t bit = 0; bit < 5; ++bit) {
    context->Add(digits[bit], digits[bit], half_offset);
    Plaintext<uint64_t> decoded;
    client.Decrypt(decoded, digits[bit]);
    std::vector<Complex> values;
    context->encoder_.Decode(values, decoded);
    double bit_error = 0;
    for (size_t index = 0; index < values.size(); ++index) {
      double expected = std::stod(rows[index % rows.size()].at("extract")[bit].get<std::string>());
      double err = std::abs(values[index] - expected);
      bit_error = std::max(bit_error, err);
      max_error = std::max(max_error, err);
    }
    std::cout << "bit " << bit << " max_error=" << bit_error << "\n";
  }
  std::cout << "Total max_error=" << max_error << "\n";
  EXPECT_LT(max_error, std::exp2(-10));

  // Now perform the 2 cleaning iterations with intermediate rescales:
  for (int bit = 0; bit < 5; ++bit) {
    // Iteration 1 (at level 13)
    {
      const int cur_level = 13;
      Constant<uint64_t> cubic_sq, cubic_cb;
      const auto np = context->param_.LevelToNP(cur_level);
      const auto primes = context->param_.GetPrimeVector(np);
      HostVector<uint64_t> residues(primes.size());
      for (size_t i = 0; i < primes.size(); ++i) {
        residues[i] = primeutil::ToMontgomery(
            uint64_t((static_cast<unsigned __int128>(uint64_t(scale)) * 3) % primes[i]), primes[i]);
      }
      cubic_sq.ModifyNP(np);
      cubic_sq.SetScale(scale);
      CopyHostToDevice(cubic_sq.cx_, residues);
      context->encoder_.EncodeConstant(cubic_cb, cur_level, 1.0, 2.0);

      Ciphertext<uint64_t> sq, cb, sc_sq, tw_cb, poly;
      context->Mult(sq, digits[bit], digits[bit]);
      context->Relinearize(sq, sq, client.GetMultiplicationKey());
      context->Mult(cb, digits[bit], sq);
      context->Relinearize(cb, cb, client.GetMultiplicationKey());
      context->Mult(sc_sq, sq, cubic_sq);
      context->Mult(tw_cb, cb, cubic_cb);
      context->Sub(poly, sc_sq, tw_cb);
      Ciphertext<uint64_t> r1, r2;
      context->Rescale(r1, poly); // L13 -> L12, scale ~ 2^80
      context->Rescale(r2, r1);   // L12 -> L11, scale ~ 2^40
      digits[bit] = std::move(r2);
    }
    // Check clean 1 error
    {
      Plaintext<uint64_t> dec;
      client.Decrypt(dec, digits[bit]);
      std::vector<Complex> vals;
      context->encoder_.Decode(vals, dec);
      double c1_err = 0;
      for (size_t i = 0; i < vals.size(); ++i) {
        double exp = std::stod(rows[i % rows.size()].at("clean")[0][bit].get<std::string>());
        c1_err = std::max(c1_err, std::abs(vals[i] - exp));
      }
      std::cout << "bit " << bit << " clean1_error=" << c1_err << "\n";
      EXPECT_LT(c1_err, std::exp2(-16));
    }

    // Iteration 2 (at level 11)
    {
      const int cur_level = 11;
      const double cur_scale = digits[bit].GetScale();
      Constant<uint64_t> cubic_sq, cubic_cb;
      const auto np = context->param_.LevelToNP(cur_level);
      const auto primes = context->param_.GetPrimeVector(np);
      HostVector<uint64_t> residues(primes.size());
      for (size_t i = 0; i < primes.size(); ++i) {
        residues[i] = primeutil::ToMontgomery(
            uint64_t((static_cast<unsigned __int128>(uint64_t(cur_scale)) * 3) % primes[i]), primes[i]);
      }
      cubic_sq.ModifyNP(np);
      cubic_sq.SetScale(cur_scale);
      CopyHostToDevice(cubic_sq.cx_, residues);
      context->encoder_.EncodeConstant(cubic_cb, cur_level, 1.0, 2.0);

      Ciphertext<uint64_t> sq, cb, sc_sq, tw_cb, poly;
      context->Mult(sq, digits[bit], digits[bit]);
      context->Relinearize(sq, sq, client.GetMultiplicationKey());
      context->Mult(cb, digits[bit], sq);
      context->Relinearize(cb, cb, client.GetMultiplicationKey());
      context->Mult(sc_sq, sq, cubic_sq);
      context->Mult(tw_cb, cb, cubic_cb);
      context->Sub(poly, sc_sq, tw_cb);
      Ciphertext<uint64_t> r1, r2;
      context->Rescale(r1, poly); // L11 -> L10, scale ~ 2^80
      context->Rescale(r2, r1);   // L10 -> L9, scale ~ 2^40
      digits[bit] = std::move(r2);
    }
    // Check clean 2 error
    {
      Plaintext<uint64_t> dec;
      client.Decrypt(dec, digits[bit]);
      std::vector<Complex> vals;
      context->encoder_.Decode(vals, dec);
      double c2_err = 0;
      for (size_t i = 0; i < vals.size(); ++i) {
        double exp = std::stod(rows[i % rows.size()].at("clean")[1][bit].get<std::string>());
        c2_err = std::max(c2_err, std::abs(vals[i] - exp));
      }
      std::cout << "bit " << bit << " clean2_error=" << c2_err << "\n";
      EXPECT_LT(c2_err, std::exp2(-33));
    }
  }

  // Recombination:
  const int out_level = 9;
  const double out_scale = digits[0].GetScale();
  Ciphertext<uint64_t> accum;
  Constant<uint64_t> weight0;
  context->encoder_.EncodeConstant(weight0, out_level, 1.0, 1.0);
  context->Mult(accum, digits[0], weight0);
  for (int bit = 1; bit < 5; ++bit) {
    Constant<uint64_t> weight;
    context->encoder_.EncodeConstant(weight, out_level, 1.0, double(1 << bit));
    Ciphertext<uint64_t> term;
    context->Mult(term, digits[bit], weight);
    context->Add(accum, accum, term);
  }
  Constant<uint64_t> signed_offset;
  context->encoder_.EncodeConstant(signed_offset, out_level, out_scale, 16.0);
  Ciphertext<uint64_t> combined;
  context->Sub(combined, accum, signed_offset);

  Plaintext<uint64_t> final_dec;
  client.Decrypt(final_dec, combined);
  std::vector<Complex> final_vals;
  context->encoder_.Decode(final_vals, final_dec);
  double max_int_err = 0;
  for (size_t i = 0; i < final_vals.size(); ++i) {
    double exp = double(rows[i % rows.size()].at("integer").get<int>());
    max_int_err = std::max(max_int_err, std::abs(final_vals[i] - exp));
  }
  std::cout << "Combined max_integer_error=" << max_int_err
            << " output_bits=" << -std::log2(max_int_err) << "\n";
  EXPECT_LT(max_int_err, std::exp2(-29));
}
