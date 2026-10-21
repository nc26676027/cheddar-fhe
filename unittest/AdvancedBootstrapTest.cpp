#include <gtest/gtest.h>

#include "extension/BootParameter.h"
#include "extension/BootContext.h"
#include "extension/DiscreteIdentityBootstrap.h"

using namespace cheddar;

TEST(AdvancedBootstrap, NamedIdentity19Factory) {
  const auto identity = BootParameter::Identity19();

  EXPECT_EQ(identity.route, BootRoute::kIdentity19);
  EXPECT_EQ(BootParameter::Selected(BootRoute::kMEBoot).route,
            BootRoute::kIdentity19);
  EXPECT_TRUE(IsMEBootRoute(identity.route));
  EXPECT_FALSE(IsDiscreteMEBootRoute(identity.route));
  EXPECT_EQ(identity.OutputLevel(), 21);
  EXPECT_EQ(identity.k, 20);
  EXPECT_EQ(identity.double_angle, 2);
  EXPECT_EQ(identity.sparse_h, 40);
}

TEST(AdvancedBootstrap, NamedDiscreteCKKSMeBootFactory) {
  const auto discrete = BootParameter::DiscreteCKKSMeBoot();

  EXPECT_EQ(discrete.route, BootRoute::kDiscreteCKKSMeBoot);
  EXPECT_EQ(BootParameter::Selected(BootRoute::kDiscreteMEBoot).route,
            BootRoute::kDiscreteCKKSMeBoot);
  EXPECT_TRUE(IsMEBootRoute(discrete.route));
  EXPECT_TRUE(IsDiscreteMEBootRoute(discrete.route));
  EXPECT_EQ(discrete.k, 20);
  EXPECT_EQ(discrete.double_angle, 2);
  EXPECT_EQ(discrete.sparse_h, 40);
  EXPECT_EQ(discrete.extra_q, BootParameter::Identity19().extra_q);
}

TEST(AdvancedBootstrap, CertifiedRemezParameters) {
  const auto certified = DiscreteIdentityParameters::Certified();
  EXPECT_EQ(certified.width, 6);
  EXPECT_EQ(certified.extraction_baby_step, 16);
  EXPECT_EQ(certified.cleaner_alignment_level, 29);
  EXPECT_EQ(certified.extractor_coefficients.size(), 6);
  for (const auto &poly : certified.extractor_coefficients) {
    EXPECT_EQ(poly.size(), 64);
  }
}

TEST(AdvancedBootstrap, SelfContainedDiscreteContextCreationAndPrepare) {
  const auto discrete_profile = BootParameter::DiscreteCKKSMeBoot();
  const auto parameter = discrete_profile.MakeParameter();
  // One-line factory call with zero external parameters!
  auto context = BootContext<uint64_t>::Create(*parameter, discrete_profile);
  ASSERT_NE(context, nullptr);
  EXPECT_FALSE(context->IsBootPrepared(BootParameter::kSlots));
  context->Prepare();
  EXPECT_TRUE(context->IsBootPrepared(BootParameter::kSlots));
}
