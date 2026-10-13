#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <fstream>

#include "core/Context.h"

using namespace cheddar;

template <typename word>
void CheckCenteredSinglePrimeLift(const char *parameter_file) {
  std::ifstream file(std::string(PARAM_DIR) + "/" + parameter_file);
  ASSERT_TRUE(file.is_open());
  auto json = nlohmann::json::parse(file);
  std::vector<word> main, aux;
  for (int i = 0; i < 3; ++i) {
    main.push_back(json["main_primes"][i].get<word>());
    aux.push_back(json["auxiliary_primes"][i].get<word>());
  }
  Parameter<word> parameter(16, 1048576.0, 0, {{1, 0}, {2, 0}, {3, 0}}, main, aux);
  auto context = Context<word>::Create(parameter);
  int degree = parameter.degree_;
  word q0 = main[0];
  int128_t half = q0 / 2;
  std::vector<int128_t> cases{0, 1, half, -half, -1};
  HostVector<word> source(degree);
  for (int j = 0; j < degree; ++j) {
    auto value = cases[j % cases.size()];
    source[j] = static_cast<word>(value < 0 ? value + q0 : value);
  }
  DeviceVector<word> device_source(degree), result(main.size() * degree);
  CopyHostToDevice(device_source, source);
  auto view = result.View();
  context->elem_handler_.ModUpToMax(view, device_source.ConstView());
  HostVector<word> obtained;
  CopyDeviceToHost(obtained, result);
  for (size_t k = 0; k < main.size(); ++k) {
    for (int j = 0; j < degree; ++j) {
      int128_t expected = cases[j % cases.size()] % static_cast<int128_t>(main[k]);
      if (expected < 0) expected += main[k];
      ASSERT_EQ(obtained[k * degree + j], static_cast<word>(expected))
          << "limb " << k << ", coefficient " << j;
    }
  }
}

TEST(ModUpToMax, Word32CenteredLift) { CheckCenteredSinglePrimeLift<uint32_t>("bootparam_40.json"); }
TEST(ModUpToMax, Word64CenteredLift) { CheckCenteredSinglePrimeLift<uint64_t>("bootparam_40_64bit.json"); }
