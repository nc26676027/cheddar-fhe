#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Testbed.h"
#include "core/AKSKeyMap.h"
#include "extension/MEBootMatrixPlanner.h"

using namespace cheddar;

TEST(AKSKeyMapTest, BasicOperations) {
  AKSKeyMap<uint32_t> map;
  EXPECT_TRUE(map.empty());
  EXPECT_FALSE(map.HasKey(128));

  NPInfo np(2, 0, 1);
  map.try_emplace(128, np, 1);
  EXPECT_EQ(map.size(), 1);
  EXPECT_TRUE(map.HasKey(128));
  EXPECT_FALSE(map.HasKey(256));

  const auto &key = map.GetKey(128);
  EXPECT_EQ(key.GetBeta(), 1);

  // Test move semantics
  AKSKeyMap<uint32_t> moved_map = std::move(map);
  EXPECT_TRUE(moved_map.HasKey(128));
}

TEST_P(Testbed32, AKSKeyGeneration) {
  int num_slots = 1 << 15;
  auto plans = MEBootMatrixPlanner::PlanCtS(context_, num_slots);
  ASSERT_EQ(plans.size(), 4);

  const auto &stage2_plan = plans[1];
  EXPECT_EQ(stage2_plan.matrix.GetNumDiag(), 31);

  int aks_level = 4;
  interface_->PrepareAKSKeys(stage2_plan.matrix, aks_level,
                             default_scale_);

  const auto &aks_map = interface_->GetAKSKeyMap();
  EXPECT_EQ(aks_map.size(), 31);

  for (int offset : stage2_plan.supports) {
    EXPECT_TRUE(aks_map.HasKey(offset))
        << "Missing AKS key for offset " << offset;
    const auto &evk = aks_map.GetKey(offset);
    EXPECT_EQ(evk.GetBeta(), 1);
    EXPECT_EQ(evk.ax_.size(), 1);
    EXPECT_EQ(evk.bx_.size(), 1);
    NPInfo np = evk.GetNP();
    int expected_size = np.GetNumTotal() * context_->param_.degree_;
    EXPECT_EQ(static_cast<int>(evk.ax_[0].size()), expected_size);
    EXPECT_EQ(static_cast<int>(evk.bx_[0].size()), expected_size);
  }
}

TEST_P(Testbed64, AKSKeyGeneration) {
  int num_slots = 1 << 15;
  auto plans = MEBootMatrixPlanner::PlanCtS(context_, num_slots);
  ASSERT_EQ(plans.size(), 4);

  const auto &stage2_plan = plans[1];
  EXPECT_EQ(stage2_plan.matrix.GetNumDiag(), 31);

  int aks_level = 4;
  interface_->PrepareAKSKeys(stage2_plan.matrix, aks_level,
                             default_scale_);

  const auto &aks_map = interface_->GetAKSKeyMap();
  EXPECT_EQ(aks_map.size(), 31);

  for (int offset : stage2_plan.supports) {
    EXPECT_TRUE(aks_map.HasKey(offset))
        << "Missing AKS key for offset " << offset;
    const auto &evk = aks_map.GetKey(offset);
    EXPECT_EQ(evk.GetBeta(), 1);
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
