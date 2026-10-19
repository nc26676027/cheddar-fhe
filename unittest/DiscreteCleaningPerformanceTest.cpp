#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>

#include "UserInterface.h"
#include "extension/BootParameter.h"

using namespace cheddar;

namespace {
enum class CubicSchedule { kSeparate, kDelayed, kFactored, kAccumulated };

void EvaluateCubic(ConstContextPtr<uint64_t> context,
                   Ciphertext<uint64_t> &output,
                   const Ciphertext<uint64_t> &input,
                   const EvaluationKey<uint64_t> &key,
                   const Constant<uint64_t> &three,
                   const Constant<uint64_t> &two,
                   const Constant<uint64_t> &negative_two,
                   CubicSchedule schedule) {
  Ciphertext<uint64_t> square, cube, scaled_square, twice, polynomial;
  context->Mult(square, input, input);
  context->Relinearize(square, square, key);
  if (schedule == CubicSchedule::kAccumulated) {
    context->Mult(twice, input, negative_two);
    context->Mult(polynomial, twice, square);
    auto destination = std::vector<DvView<uint64_t>>{
        polynomial.BxView(), polynomial.AxView()};
    context->elem_handler_.CAccum(
        destination, polynomial.GetNP(),
        {square.ConstViewVector(0, true), polynomial.ConstViewVector(0, true)},
        {three.ConstView()});
  } else if (schedule == CubicSchedule::kFactored) {
    context->Mult(twice, input, two);
    context->Sub(scaled_square, three, twice);
    context->Mult(polynomial, square, scaled_square);
  } else {
    context->Mult(cube, input, square);
    if (schedule == CubicSchedule::kSeparate)
      context->Relinearize(cube, cube, key);
    context->Mult(scaled_square, square, three);
    context->Mult(twice, cube, two);
    context->Sub(polynomial, scaled_square, twice);
  }
  if (schedule == CubicSchedule::kSeparate)
    context->Rescale(output, polynomial);
  else
    context->RelinearizeRescale(output, polynomial, key);
}
}  // namespace

TEST(DiscreteIntegerCleaner, CubicPerformance) {
  if (!std::getenv("DISCRETE_CLEANING_PERF"))
    GTEST_SKIP() << "Set DISCRETE_CLEANING_PERF=1 for the matched cubic benchmark";
  const auto profile = BootParameter::DiscreteCKKSMeBoot();
  std::vector<std::pair<int, int>> levels;
  for (int count = 1; count <= int(profile.q.size()); ++count)
    levels.emplace_back(count, 0);
  Parameter<uint64_t> parameter(16, std::exp2(40), 2, levels, profile.q, profile.p);
  parameter.SetDenseHammingWeight(2048);
  parameter.SetSparseHammingWeight(2048);
  auto context = Context<uint64_t>::Create(parameter);
  context->encoder_.EnableNearestRounding();
  context->EnableCenteredModUp();
  context->EnableCenteredMergedRescale();
  UserInterface<uint64_t> client(context);
  const auto &key = client.GetMultiplicationKey();
  std::vector<Complex> values(BootParameter::kSlots);
  for (size_t index = 0; index < values.size(); ++index) {
    const double noise = (double(index % 257) / 256.0 - 0.5) / 256.0;
    values[index] = double(index % 2) + noise;
  }
  Plaintext<uint64_t> plaintext;
  context->encoder_.Encode(plaintext, 23, std::exp2(32), values);
  Ciphertext<uint64_t> input;
  client.Encrypt(input, plaintext);

  cudaEvent_t begin, end;
  ASSERT_EQ(cudaEventCreate(&begin), cudaSuccess);
  ASSERT_EQ(cudaEventCreate(&end), cudaSuccess);
  // Both iterations use the same input and key for every candidate.
  for (int iteration = 0; iteration < 2; ++iteration) {
    const int level = parameter.NPToLevel(input.GetNP());
    const double scale = std::round(input.GetScale());
    input.SetScale(scale);
    Constant<uint64_t> three, two, negative_two;
    ASSERT_LT(3 * scale, std::exp2(53));
    context->encoder_.EncodeConstant(three, level, scale, 3.0);
    context->encoder_.EncodeConstant(two, level, 1.0, 2.0);
    context->encoder_.EncodeConstant(negative_two, level, 1.0, -2.0);
    client.Decrypt(plaintext, input);
    std::vector<Complex> decoded_input;
    context->encoder_.Decode(decoded_input, plaintext);
    std::vector<CubicSchedule> schedules{
        CubicSchedule::kSeparate, CubicSchedule::kDelayed, CubicSchedule::kFactored,
        CubicSchedule::kAccumulated};
    const char *names[] = {"separate", "delayed", "factored", "accumulated"};
    std::vector<Ciphertext<uint64_t>> outputs(schedules.size());
    std::vector<std::vector<float>> samples(schedules.size());
    for (int warmup = 0; warmup < 15; ++warmup)
      for (auto schedule : schedules)
        EvaluateCubic(context, outputs[int(schedule)], input, key, three, two,
                      negative_two, schedule);
    // Interleave and reverse order so clock changes affect all candidates.
    for (int repeat = 0; repeat < 30; ++repeat) {
      for (auto schedule : schedules) {
        const int index = int(schedule);
        EXPECT_EQ(cudaDeviceSynchronize(), cudaSuccess);
        EXPECT_EQ(cudaEventRecord(begin), cudaSuccess);
        EvaluateCubic(context, outputs[index], input, key, three, two, negative_two,
                      schedule);
        EXPECT_EQ(cudaEventRecord(end), cudaSuccess);
        EXPECT_EQ(cudaEventSynchronize(end), cudaSuccess);
        float gpu_ms = 0;
        EXPECT_EQ(cudaEventElapsedTime(&gpu_ms, begin, end), cudaSuccess);
        samples[index].push_back(gpu_ms);
        std::cout << std::setprecision(12) << "DISCRETE_CUBIC_SAMPLE,"
                  << iteration << ',' << names[index] << ',' << repeat << ','
                  << gpu_ms << '\n';
      }
      std::reverse(schedules.begin(), schedules.end());
    }
    for (auto schedule : schedules) {
      const int index = int(schedule);
      const auto &output = outputs[index];
      client.Decrypt(plaintext, output);
      std::vector<Complex> decoded;
      context->encoder_.Decode(decoded, plaintext);
      EXPECT_EQ(decoded.size(), decoded_input.size());
      double max_error = 0;
      for (size_t index = 0; index < decoded.size(); ++index) {
        const auto x = decoded_input[index];
        const double error = std::abs(decoded[index] - x * x * (3.0 - 2.0 * x));
        EXPECT_TRUE(std::isfinite(error));
        max_error = std::max(max_error, error);
      }
      EXPECT_LT(max_error, std::exp2(iteration == 0 ? -16 : -33));
      EXPECT_EQ(output.GetNP(), parameter.LevelToNP(level - 1));
      context->AssertSameScale(
          output, scale * scale * scale / parameter.GetRescalePrimeProd(level));
      std::sort(samples[index].begin(), samples[index].end());
      std::cout << std::setprecision(12) << "DISCRETE_CUBIC_PERF,"
                << iteration << ',' << names[index] << ',' << level << ','
                << std::log2(scale) << ','
                << (samples[index][samples[index].size() / 2 - 1] +
                    samples[index][samples[index].size() / 2]) / 2
                << ',' << max_error << std::endl;
    }
    input = std::move(outputs[int(CubicSchedule::kSeparate)]);
  }
  EXPECT_EQ(cudaEventDestroy(begin), cudaSuccess);
  EXPECT_EQ(cudaEventDestroy(end), cudaSuccess);
}
