#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Testbed.h"
#include "extension/MEBootMatrixPlanner.h"

using json = nlohmann::json;
using namespace cheddar;

namespace {

Complex ParseGoldenComplex(const std::string &s) {
  double re = 0.0;
  double im = 0.0;
  int parsed = std::sscanf(s.c_str(), "%lf+%lfi", &re, &im);
  AssertTrue(parsed == 2, "Failed to parse golden complex string: " + s);
  return Complex(re, im);
}

void VerifyStageMatricesMatchGolden(const std::vector<MEBootStagePlan> &plans,
                                    int num_slots) {
  std::string golden_path = std::string(PARAM_DIR) + "/meboot_golden_4434.json";
  std::ifstream golden_file(golden_path);
  ASSERT_TRUE(golden_file.is_open())
      << "Could not open golden file: " << golden_path;

  json golden_data;
  golden_file >> golden_data;

  ASSERT_EQ(plans.size(), golden_data["stages"].size());
  constexpr double kGoldenStageNorm = 0.0625;  // 1/16 per stage in golden export
  constexpr double kTol = 1e-6;

  for (size_t i = 0; i < plans.size(); i++) {
    const auto &stage_json = golden_data["stages"][i];
    int expected_num_diag = stage_json["num_diagonals"];
    const auto &sample_coeffs = stage_json["sample_coeffs"];

    EXPECT_EQ(plans[i].stage_index, static_cast<int>(i));
    EXPECT_EQ(plans[i].supports.size(), static_cast<size_t>(expected_num_diag));
    EXPECT_EQ(plans[i].matrix.GetNumDiag(), expected_num_diag);

    for (int offset : plans[i].supports) {
      ASSERT_TRUE(plans[i].matrix.find(offset) != plans[i].matrix.end())
          << "Stage " << i << " missing diagonal offset " << offset;
      const auto &diag = plans[i].matrix.at(offset);
      EXPECT_EQ(static_cast<int>(diag.size()), num_slots);

      std::string key = std::to_string(offset);
      ASSERT_TRUE(sample_coeffs.contains(key))
          << "Golden stage " << i << " missing sample_coeffs key " << key;
      const auto &samples = sample_coeffs[key];
      ASSERT_EQ(samples.size(), 2u);

      for (size_t slot_idx = 0; slot_idx < 2; slot_idx++) {
        Complex golden_val =
            ParseGoldenComplex(samples[slot_idx].get<std::string>());
        Complex actual_val = diag[slot_idx] * kGoldenStageNorm;
        EXPECT_NEAR(actual_val.real(), golden_val.real(), kTol)
            << "Stage " << i << " offset " << offset << " slot " << slot_idx
            << " real mismatch";
        EXPECT_NEAR(std::abs(actual_val.imag()), std::abs(golden_val.imag()),
                    kTol)
            << "Stage " << i << " offset " << offset << " slot " << slot_idx
            << " imag magnitude mismatch";
        if (std::abs(golden_val.imag()) > kTol) {
          EXPECT_NEAR(actual_val.imag(), golden_val.imag(), kTol)
              << "Stage " << i << " offset " << offset << " slot " << slot_idx
              << " imag sign mismatch";
        }
      }
    }
  }
}

}  // namespace

TEST(MEBootMatrixPlannerTest, TheoreticalSupportsMatchGolden) {
  std::string golden_path = std::string(PARAM_DIR) + "/meboot_golden_4434.json";
  std::ifstream golden_file(golden_path);
  ASSERT_TRUE(golden_file.is_open())
      << "Could not open golden file: " << golden_path;

  json golden_data;
  golden_file >> golden_data;

  int log_slots = golden_data["log_slots"];
  std::vector<int> widths = golden_data["widths"].get<std::vector<int>>();

  auto computed_supports =
      MEBootMatrixPlanner::ComputeSupports(log_slots, widths);

  ASSERT_EQ(computed_supports.size(), golden_data["stages"].size());

  for (size_t i = 0; i < computed_supports.size(); i++) {
    const auto &stage = golden_data["stages"][i];
    int expected_num_diag = stage["num_diagonals"];
    const auto &expected_offsets =
        stage["offsets"].get<std::vector<int>>();

    EXPECT_EQ(computed_supports[i].size(), expected_num_diag)
        << "Mismatch in diagonal count for stage " << i;

    EXPECT_EQ(computed_supports[i], expected_offsets)
        << "Mismatch in diagonal offsets for stage " << i;
  }
}

TEST_P(Testbed32, MEBootMatrixPlannerCtSGeneration) {
  int num_slots = 1 << 15;  // 32768 slots
  std::vector<int> widths = {4, 4, 3, 4};
  std::vector<int> bases = {64, 8};

  auto plans =
      MEBootMatrixPlanner::PlanCtS(context_, num_slots, widths, bases, 1.0);

  ASSERT_EQ(plans.size(), 4);
  VerifyStageMatricesMatchGolden(plans, num_slots);

  // Check BSGS bases for dense stages
  EXPECT_EQ(plans[2].bs, 64);
  EXPECT_EQ(plans[3].bs, 8);
}

TEST_P(Testbed64, MEBootMatrixPlannerCtSGeneration) {
  int num_slots = 1 << 15;  // 32768 slots
  std::vector<int> widths = {4, 4, 3, 4};
  std::vector<int> bases = {64, 8};

  auto plans =
      MEBootMatrixPlanner::PlanCtS(context_, num_slots, widths, bases, 1.0);

  ASSERT_EQ(plans.size(), 4);
  VerifyStageMatricesMatchGolden(plans, num_slots);

  EXPECT_EQ(plans[2].bs, 64);
  EXPECT_EQ(plans[3].bs, 8);
}

INSTANTIATE_TEST_SUITE_P(
    Cheddar, Testbed32, testing::Values("bootparam_40.json"),
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

