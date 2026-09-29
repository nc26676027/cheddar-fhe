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

  // Expected diagonal counts for each stage
  std::vector<int> expected_diag_counts = {16, 31, 15, 31};

  for (size_t i = 0; i < plans.size(); i++) {
    EXPECT_EQ(plans[i].stage_index, i);
    EXPECT_EQ(plans[i].width, widths[i]);
    EXPECT_EQ(plans[i].supports.size(), expected_diag_counts[i]);
    EXPECT_EQ(plans[i].matrix.GetNumDiag(), expected_diag_counts[i]);

    // Check that every support offset exists in the StripedMatrix
    for (int offset : plans[i].supports) {
      EXPECT_TRUE(plans[i].matrix.find(offset) != plans[i].matrix.end())
          << "Stage " << i << " missing diagonal offset " << offset;
      const auto &diag = plans[i].matrix.at(offset);
      EXPECT_EQ(diag.size(), num_slots);
    }
  }

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
  std::vector<int> expected_diag_counts = {16, 31, 15, 31};

  for (size_t i = 0; i < plans.size(); i++) {
    EXPECT_EQ(plans[i].stage_index, i);
    EXPECT_EQ(plans[i].width, widths[i]);
    EXPECT_EQ(plans[i].supports.size(), expected_diag_counts[i]);
    EXPECT_EQ(plans[i].matrix.GetNumDiag(), expected_diag_counts[i]);
  }
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

