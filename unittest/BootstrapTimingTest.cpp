#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <string>

#include "extension/BootContext.h"
#include "extension/BootstrapTiming.h"

using namespace cheddar;

TEST(BootstrapTiming, GrowsForDetailedStagesAndCopiesNames) {
  const auto result = detail::BootstrapTiming::Measure<BootProfile>(
      [](const auto &mark) {
        for (int i = 0; i < 128; ++i) {
          const std::string name = "clean/" + std::to_string(i);
          mark(name.c_str());
        }
      });
  ASSERT_EQ(result.stages.size(), 129);
  double gpu_sum = 0;
  double host_sum = 0;
  for (size_t i = 0; i < result.stages.size(); ++i) {
    const auto &stage = result.stages[i];
    EXPECT_EQ(stage.name, i == 128 ? "ReleaseTemps" :
                                   "clean/" + std::to_string(i));
    EXPECT_TRUE(std::isfinite(stage.gpu_ms));
    EXPECT_GE(stage.gpu_ms, 0);
    EXPECT_TRUE(std::isfinite(stage.host_submission_ms));
    EXPECT_GE(stage.host_submission_ms, 0);
    gpu_sum += stage.gpu_ms;
    host_sum += stage.host_submission_ms;
  }
  EXPECT_NEAR(gpu_sum, result.gpu_ms, 0.001);
  EXPECT_TRUE(std::isfinite(result.wall_ms));
  EXPECT_GE(result.wall_ms, host_sum);
}

TEST(BootstrapTiming, EmptyEvaluationStillMeasuresRelease) {
  const auto result = detail::BootstrapTiming::Measure<BootProfile>(
      [](const auto &) {});
  ASSERT_EQ(result.stages.size(), 1);
  EXPECT_EQ(result.stages.front().name, "ReleaseTemps");
  EXPECT_GE(result.gpu_ms, 0);
  EXPECT_GE(result.wall_ms, 0);
}

TEST(BootstrapTiming, ThrowingEvaluationUnwindsAndCanBeRetried) {
  for (int i = 0; i < 16; ++i) {
    EXPECT_THROW(detail::BootstrapTiming::Measure<BootProfile>(
        [](const auto &mark) {
          mark("before-error");
          throw std::runtime_error("evaluation failed");
        }), std::runtime_error);
  }
  const auto result = detail::BootstrapTiming::Measure<BootProfile>(
      [](const auto &mark) { mark("recovered"); });
  ASSERT_EQ(result.stages.size(), 2);
  EXPECT_EQ(result.stages.front().name, "recovered");
  EXPECT_EQ(result.stages.back().name, "ReleaseTemps");
  EXPECT_EQ(cudaDeviceSynchronize(), cudaSuccess);
}
