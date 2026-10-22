#include <gtest/gtest.h>

#include "extension/BootParameter.h"
#include "extension/BootContext.h"
#include "extension/MEBootMatrixPlanner.h"
#include "extension/Func15RBootCoefficients.inc"

namespace cheddar {
namespace {

TEST(ParameterFunc15Test, BootParameterFactoryAndFields) {
  auto boot_param = BootParameter::Func15RBoot();
  EXPECT_EQ(boot_param.route, BootRoute::kFunc15RBoot);
  EXPECT_EQ(boot_param.log_degree, 15);
  EXPECT_EQ(boot_param.slots, 16384);
  EXPECT_DOUBLE_EQ(boot_param.application_scale, std::exp2(43.0));
  EXPECT_EQ(boot_param.OutputLevel(), 3);
  EXPECT_EQ(boot_param.q.size(), 19u);
  EXPECT_EQ(boot_param.p.size(), 2u);
  EXPECT_EQ(boot_param.sparse_h, 80);
  EXPECT_EQ(boot_param.extension, 8);
  EXPECT_EQ(boot_param.k, 22);
  EXPECT_EQ(boot_param.double_angle, 2);
  EXPECT_EQ(boot_param.mod1_level, 16);
  EXPECT_EQ(boot_param.widths, (std::vector<int>{3, 3, 4, 4}));
  EXPECT_EQ(boot_param.bases, (std::vector<int>{64, 4}));
  EXPECT_EQ(boot_param.extra_q, 9007199255658497ull);

  // Verify first and last primes match paper
  EXPECT_EQ(boot_param.q[0], 17179672577ull);
  EXPECT_EQ(boot_param.q[1], 536608769ull);
  EXPECT_EQ(boot_param.q[18], 2146959361ull);
  EXPECT_EQ(boot_param.p[0], 562949952700417ull);
  EXPECT_EQ(boot_param.p[1], 562949954142209ull);

  // Validate scaling binding
  EXPECT_NO_THROW(boot_param.ValidateScalingBinding());
}

TEST(ParameterFunc15Test, MakeParameterAndContextCreation) {
  auto boot_param = BootParameter::Func15RBoot();
  auto param = boot_param.MakeParameter();

  ASSERT_NE(param, nullptr);
  EXPECT_EQ(param->log_degree_, 15);
  EXPECT_EQ(param->degree_, 32768);
  EXPECT_EQ(param->default_encryption_level_, 3);
  EXPECT_EQ(param->max_key_switch_level_, 18);
  EXPECT_EQ(param->GetSparseHammingWeight(), 80);
  EXPECT_EQ(param->GetDenseHammingWeight(), 1024);
  EXPECT_EQ(param->entry_primes_.size(), 1u);
  EXPECT_EQ(param->entry_primes_[0], 786433ull);
  EXPECT_EQ(param->ter_primes_.size(), 1u);
  EXPECT_EQ(param->ter_primes_[0], 9007199255658497ull);

  // Context creation
  std::shared_ptr<BootContext<uint64_t>> context;
  EXPECT_NO_THROW({
    context = BootContext<uint64_t>::Create(*param, boot_param);
  });
  ASSERT_NE(context, nullptr);
  EXPECT_EQ(context->param_.log_degree_, 15);
  EXPECT_EQ(context->param_.degree_, 32768);
}

TEST(ParameterFunc15Test, MEBootMatrixPlannerSupportsAndStages) {
  int log_slots = 14;
  std::vector<int> widths = {3, 3, 4, 4};
  auto supports = MEBootMatrixPlanner::ComputeSupports(log_slots, widths);

  ASSERT_EQ(supports.size(), 4u);
  // Stage 0: width 3, stride 2^11 = 2048. Multiples modulo 16384 wrap to 16384/2048 = 8 values
  EXPECT_EQ(supports[0].size(), 8u);
  // Stage 1: width 3, stride 2^8 = 256, shift in [-7, 7] -> 15 distinct rotations
  EXPECT_EQ(supports[1].size(), 15u);
  // Stage 2: width 4, stride 2^4 = 16, shift in [-15, 15] -> 31 distinct rotations
  EXPECT_EQ(supports[2].size(), 31u);
  // Stage 3: width 4, stride 2^0 = 1, shift in [-15, 15] -> 31 distinct rotations
  EXPECT_EQ(supports[3].size(), 31u);

  // Test full PlanCtS
  auto boot_param = BootParameter::Func15RBoot();
  auto param = boot_param.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, boot_param);
  ConstContextPtr<uint64_t> const_ctx = context;

  std::vector<MEBootStagePlan> plans;
  EXPECT_NO_THROW({
    plans = MEBootMatrixPlanner::PlanCtS(const_ctx, 16384, widths, {64, 4});
  });
  ASSERT_EQ(plans.size(), 4u);
  EXPECT_EQ(plans[0].stage_index, 0);
  EXPECT_EQ(plans[0].width, 3);
  EXPECT_EQ(plans[1].stage_index, 1);
  EXPECT_EQ(plans[1].width, 3);
  EXPECT_EQ(plans[2].stage_index, 2);
  EXPECT_EQ(plans[2].width, 4);
  EXPECT_EQ(plans[2].bs, 64);
  EXPECT_EQ(plans[3].stage_index, 3);
  EXPECT_EQ(plans[3].width, 4);
  EXPECT_EQ(plans[3].bs, 4);
}

TEST(ParameterFunc15Test, InlinedConstantsMatchExport) {
  EXPECT_DOUBLE_EQ(func15_rboot::kK, 22.0);
  EXPECT_DOUBLE_EQ(func15_rboot::kSqrt2Pi, 1.0);
  EXPECT_EQ(func15_rboot::kDoubleAngle, 2);
  EXPECT_DOUBLE_EQ(func15_rboot::kOutputShift, 0.0546875);
  EXPECT_DOUBLE_EQ(func15_rboot::kActivationGain, 120.0);
  EXPECT_EQ(func15_rboot::kMainDegree, 62);
  EXPECT_EQ(func15_rboot::kOddTailDegree, 15);
  EXPECT_EQ(func15_rboot::kEvenArcDegree, 15);
}

}  // namespace
}  // namespace cheddar
