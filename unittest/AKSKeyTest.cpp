#include <gtest/gtest.h>

#include <cmath>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "Testbed.h"
#include "core/AKSKeyMap.h"
#include "core/EvkMap.h"
#include "extension/MEBootMatrixPlanner.h"

using namespace cheddar;

static_assert(
    !std::is_base_of_v<std::unordered_map<int, EvaluationKey<uint32_t>>,
                       AKSKeyMap<uint32_t>>,
    "AKSKeyMap must not publicly inherit from std::unordered_map");
static_assert(
    !std::is_base_of_v<std::unordered_map<int, EvaluationKey<uint32_t>>,
                       EvkMap<uint32_t>>,
    "EvkMap must not publicly inherit from std::unordered_map");

TEST(AKSKeyMapTest, BasicOperations) {
  AKSKeyMap<uint32_t> map;
  EXPECT_TRUE(map.Empty());
  EXPECT_FALSE(map.HasKey(128));

  NPInfo np(2, 0, 1);
  map.EmplaceKey(128, np, 1);
  EXPECT_EQ(map.Size(), 1u);
  EXPECT_TRUE(map.HasKey(128));
  EXPECT_FALSE(map.HasKey(256));

  const auto &key = map.GetKey(128);
  EXPECT_EQ(key.GetBeta(), 1);

  // Test move semantics
  AKSKeyMap<uint32_t> moved_map = std::move(map);
  EXPECT_TRUE(moved_map.HasKey(128));
}

TEST(EvkMapTest, TypedKeyStoreEncapsulation) {
  EvkMap<uint32_t> store;
  EXPECT_FALSE(store.HasMultiplicationKey());
  EXPECT_FALSE(store.HasConjugationKey());
  EXPECT_FALSE(store.HasDenseToSparseKey());
  EXPECT_FALSE(store.HasSparseToDenseKey());
  EXPECT_FALSE(store.HasRotationKey(16));
  EXPECT_FALSE(store.HasAKSKey(128));

  NPInfo np(3, 0, 1);
  store.EmplaceMultiplicationKey(np, 3);
  store.EmplaceConjugationKey(np, 3);
  store.EmplaceRotationKey(16, np, 3);
  store.EmplaceAKSKey(128, np, 1);

  EXPECT_TRUE(store.HasMultiplicationKey());
  EXPECT_EQ(store.GetMultiplicationKey().GetBeta(), 3);
  EXPECT_TRUE(store.HasConjugationKey());
  EXPECT_EQ(store.GetConjugationKey().GetBeta(), 3);
  EXPECT_TRUE(store.HasRotationKey(16));
  EXPECT_EQ(store.GetRotationKey(16).GetBeta(), 3);
  EXPECT_TRUE(store.HasAKSKey(128));
  EXPECT_EQ(store.GetAKSKey(128).GetBeta(), 1);
  EXPECT_EQ(store.GetAKSKeyMap().Size(), 1u);
}

TEST_P(Testbed32, AKSKeyGeneration) {
  int num_slots = 1 << 15;
  auto plans = MEBootMatrixPlanner::PlanCtS(context_, num_slots);
  ASSERT_EQ(plans.size(), 4u);

  const auto &stage2_plan = plans[1];
  EXPECT_EQ(stage2_plan.matrix.GetNumDiag(), 31);

  int aks_level = 4;
  interface_->PrepareAKSKeys(stage2_plan.matrix, aks_level,
                             default_scale_);

  const auto &evk_map = interface_->GetEvkMap();
  const auto &aks_map = interface_->GetAKSKeyMap();
  EXPECT_EQ(aks_map.Size(), 31u);
  EXPECT_EQ(evk_map.GetAKSKeyMap().Size(), 31u);

  for (int offset : stage2_plan.supports) {
    EXPECT_TRUE(aks_map.HasKey(offset))
        << "Missing AKS key for offset " << offset;
    EXPECT_TRUE(evk_map.HasAKSKey(offset))
        << "Missing AKS key in unified EvkMap for offset " << offset;
    const auto &evk = evk_map.GetAKSKey(offset);
    EXPECT_EQ(evk.GetBeta(), 1);
    EXPECT_EQ(evk.ax_.size(), 1u);
    EXPECT_EQ(evk.bx_.size(), 1u);
    NPInfo np = evk.GetNP();
    int expected_size = np.GetNumTotal() * context_->param_.degree_;
    EXPECT_EQ(static_cast<int>(evk.ax_[0].size()), expected_size);
    EXPECT_EQ(static_cast<int>(evk.bx_[0].size()), expected_size);
  }
}

TEST_P(Testbed64, AKSKeyGeneration) {
  int num_slots = 1 << 15;
  auto plans = MEBootMatrixPlanner::PlanCtS(context_, num_slots);
  ASSERT_EQ(plans.size(), 4u);

  const auto &stage2_plan = plans[1];
  EXPECT_EQ(stage2_plan.matrix.GetNumDiag(), 31);

  int aks_level = 4;
  interface_->PrepareAKSKeys(stage2_plan.matrix, aks_level,
                             default_scale_);

  const auto &evk_map = interface_->GetEvkMap();
  const auto &aks_map = interface_->GetAKSKeyMap();
  EXPECT_EQ(aks_map.Size(), 31u);

  for (int offset : stage2_plan.supports) {
    EXPECT_TRUE(evk_map.HasAKSKey(offset))
        << "Missing AKS key for offset " << offset;
    const auto &evk = evk_map.GetAKSKey(offset);
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
