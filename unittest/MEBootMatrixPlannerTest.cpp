#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <limits>
#include <zlib.h>

#include "Testbed.h"
#include "extension/MEBootMatrixPlanner.h"

using json = nlohmann::json;
using namespace cheddar;

namespace {

uint64_t ReadLittleEndian(gzFile file, int bytes) {
  unsigned char data[8];
  AssertTrue(gzread(file, data, bytes) == bytes, "Truncated golden fixture");
  uint64_t value = 0;
  for (int i = 0; i < bytes; ++i) {
    value |= uint64_t(data[i]) << (8 * i);
  }
  return value;
}

double ReadGoldenDouble(gzFile file) {
  uint64_t bits = ReadLittleEndian(file, 8);
  double value;
  static_assert(sizeof(value) == sizeof(bits));
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void VerifyStageMatricesMatchGolden(const std::vector<MEBootStagePlan> &plans,
                                    int num_slots) {
  std::string golden_path = std::string(PARAM_DIR) + "/meboot_golden_4434.bin.gz";
  gzFile golden_file = gzopen(golden_path.c_str(), "rb");
  ASSERT_NE(golden_file, nullptr) << "Could not open golden file: " << golden_path;
  // Close the stream even if a fatal GoogleTest assertion returns early.
  auto close_file = [](gzFile_s *file) { gzclose(file); };
  std::unique_ptr<gzFile_s, decltype(close_file)> stream(golden_file, close_file);
  char magic[8];
  ASSERT_EQ(gzread(golden_file, magic, 8), 8);
  ASSERT_EQ(std::string(magic, 8), "MEBCTSG1");
  ASSERT_EQ(ReadLittleEndian(golden_file, 4), num_slots);
  ASSERT_EQ(ReadLittleEndian(golden_file, 4), plans.size());
  constexpr double kGoldenStageNorm = 0.0625;  // 1/16 per stage in golden export
  constexpr double kTol = 1e-12;
  size_t compared = 0;

  for (size_t i = 0; i < plans.size(); i++) {
    int expected_num_diag = ReadLittleEndian(golden_file, 4);
    EXPECT_EQ(plans[i].stage_index, static_cast<int>(i));
    EXPECT_EQ(plans[i].supports.size(), static_cast<size_t>(expected_num_diag));
    EXPECT_EQ(plans[i].matrix.GetNumDiag(), expected_num_diag);

    double max_error = 0;
    int worst_offset = 0, worst_slot = 0;
    for (int diagonal = 0; diagonal < expected_num_diag; ++diagonal) {
      int offset = ReadLittleEndian(golden_file, 4);
      ASSERT_EQ(offset, plans[i].supports.at(diagonal));
      ASSERT_TRUE(plans[i].matrix.find(offset) != plans[i].matrix.end())
          << "Stage " << i << " missing diagonal offset " << offset;
      const auto &diag = plans[i].matrix.at(offset);
      ASSERT_EQ(static_cast<int>(diag.size()), num_slots);
      for (int slot_idx = 0; slot_idx < num_slots; ++slot_idx) {
        double real = ReadGoldenDouble(golden_file);
        double imag = ReadGoldenDouble(golden_file);
        Complex golden_val(real, imag);
        Complex actual_val = diag[slot_idx] * kGoldenStageNorm;
        double error = std::abs(actual_val - golden_val);
        if (!std::isfinite(error)) error = std::numeric_limits<double>::infinity();
        if (error > max_error) {
          max_error = error;
          worst_offset = offset;
          worst_slot = slot_idx;
        }
        ++compared;
      }
    }
    EXPECT_LE(max_error, kTol) << "Stage " << i << " offset " << worst_offset
                              << " slot " << worst_slot;
    std::cout << "Golden stage " << i << ": " << expected_num_diag * num_slots
              << " coefficients, max complex error=" << max_error << '\n';
  }
  unsigned char extra;
  EXPECT_EQ(gzread(golden_file, &extra, 1), 0) << "Trailing golden data";
  int z_error;
  gzerror(golden_file, &z_error);
  EXPECT_EQ(z_error, Z_OK);
  EXPECT_EQ(compared, 3047424u);
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

