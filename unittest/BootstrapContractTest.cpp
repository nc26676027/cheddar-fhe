#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "extension/BootContext.h"

using namespace cheddar;

namespace {
DiscreteIdentityParameters RemezParameters() {
  std::ifstream stream(std::string(PARAM_DIR) +
                       "/discrete_direct6_k21_remez_d63.json");
  if (!stream) throw std::runtime_error("Missing certified Remez manifest");
  const auto manifest = nlohmann::json::parse(stream);
  std::vector<std::vector<double>> coefficients;
  for (const auto &row : manifest.at("coefficients")) {
    std::vector<double> values;
    for (const auto &value : row)
      values.push_back(std::stod(value.get<std::string>()));
    coefficients.push_back(std::move(values));
  }
  return DiscreteIdentityParameters::FromHW40RemezCoefficients(
      std::move(coefficients), manifest.at("argument_map").at("center_divisor"),
      manifest.at("argument_map").at("encoded_affine_scale"),
      manifest.at("argument_map").at("encoded_affine_integer"));
}

struct Schedule {
  int alignment, first_rescales, second_rescales;
  double input_scale;
  int output_level;
};

class BootstrapContract : public testing::TestWithParam<Schedule> {};

TEST_P(BootstrapContract, PreparesDiscreteSchedule) {
  const auto schedule = GetParam();
  const auto profile = BootParameter::DiscreteCKKSMeBoot();
  const auto parameter = profile.MakeParameter();
  auto settings = RemezParameters();
  settings.cleaner_alignment_level = schedule.alignment;
  settings.cleaning_rescales = {schedule.first_rescales, schedule.second_rescales};
  if (schedule.input_scale) settings.input_scale = schedule.input_scale;
  auto context = BootContext<uint64_t>::Create(*parameter, profile, settings);
  EXPECT_FALSE(context->IsBootPrepared(BootParameter::kSlots));
  context->Prepare();
  context->Prepare();
  EXPECT_TRUE(context->IsBootPrepared(BootParameter::kSlots));
  EXPECT_EQ(context->GetRouteExecution().OutputLevel(), schedule.output_level);
  EXPECT_EQ(context->GetRouteExecution().OutputScale(), std::exp2(40));
  // Native bypass capacity is independently fixed, not the discrete result.
  EXPECT_EQ(profile.OutputLevel(), 21);
  EXPECT_EQ(parameter->default_encryption_level_, 21);
}

INSTANTIATE_TEST_SUITE_P(
    Supported, BootstrapContract,
    testing::Values(Schedule{29, 1, 1, 0, 20},
                    Schedule{28, 1, 1, 0, 19},
                    Schedule{29, 1, 2, 0, 19},
                    Schedule{29, 1, 1, std::exp2(59), 19}));

TEST(BootstrapContractDeathTest, RejectsInvalidSchedulesBeforeEvaluation) {
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  const auto profile = BootParameter::DiscreteCKKSMeBoot();
  const auto parameter = profile.MakeParameter();
  const auto valid = RemezParameters();
  for (int level : {20, 30}) {
    auto settings = valid;
    settings.cleaner_alignment_level = level;
    EXPECT_EXIT({ BootContext<uint64_t>::Create(*parameter, profile, settings); },
                testing::ExitedWithCode(EXIT_FAILURE),
                "Unsupported discrete cleaner alignment level");
  }
  for (const auto &counts : {std::vector<int>{1}, std::vector<int>{0, 1},
                            std::vector<int>{1, 3}}) {
    auto settings = valid;
    settings.cleaning_rescales = counts;
    EXPECT_EXIT({ BootContext<uint64_t>::Create(*parameter, profile, settings); },
                testing::ExitedWithCode(EXIT_FAILURE),
                "cleaning iterations|cleaning rescale schedule");
  }
  auto settings = valid;
  settings.input_scale = std::numeric_limits<double>::infinity();
  EXPECT_EXIT({ BootContext<uint64_t>::Create(*parameter, profile, settings); },
              testing::ExitedWithCode(EXIT_FAILURE),
              "Invalid discrete identity profile");
}

TEST(BootstrapContractDeathTest, RejectsIncompatibleRouteAndManifest) {
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  const auto settings = RemezParameters();
  const auto native = BootParameter::Identity19();
  const auto native_parameter = native.MakeParameter();
  EXPECT_EXIT({
    BootContext<uint64_t>::Create(*native_parameter, native, settings);
  }, testing::ExitedWithCode(EXIT_FAILURE),
     "Discrete parameters require the discrete MEBOOT route");

  const auto discrete = BootParameter::DiscreteCKKSMeBoot();
  const auto parameter = discrete.MakeParameter();
  // Under ADR-0011, self-contained creation without extra parameters succeeds with certified defaults.
  auto discrete_context = BootContext<uint64_t>::Create(*parameter, discrete);
  EXPECT_NE(discrete_context, nullptr);
  auto wrong = discrete;
  wrong.extra_q = native.q[0];
  const auto wrong_parameter = wrong.MakeParameter();
  EXPECT_EXIT({
    BootContext<uint64_t>::Create(*wrong_parameter, discrete, settings);
  }, testing::ExitedWithCode(EXIT_FAILURE),
     "Bootstrap modulus contract mismatch");
}

TEST(BootstrapContractDeathTest, RejectsInsufficientBypassCapacity) {
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  auto profile = BootParameter::DiscreteCKKSMeBoot();
  profile.mod1_level = 28;
  const auto parameter = profile.MakeParameter();
  const auto settings = RemezParameters();
  EXPECT_EXIT({
    auto context = BootContext<uint64_t>::Create(*parameter, profile, settings);
    context->Prepare();
  }, testing::ExitedWithCode(EXIT_FAILURE),
     "Integer cleaner cannot return above the accurate bypass");
}
}  // namespace
