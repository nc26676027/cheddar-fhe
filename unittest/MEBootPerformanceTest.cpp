#include "Testbed.h"
#include "extension/BootContext.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>

namespace {

using Profile = BootParameter;

BootRoute SelectedRoute() {
  const char *value = std::getenv("MEBOOT_PERF_ROUTE");
  if (value && std::string(value) == "ordinary")
    return BootRoute::kOrdinary;
  return BootRoute::kMEBoot;
}

int EnvInt(const char *name, int fallback) {
  const char *value = std::getenv(name);
  return value ? std::max(1, std::atoi(value)) : fallback;
}

TEST(MEBootIdentity19, PublicBootstrapPerformance) {
  if (!std::getenv("MEBOOT_PERF"))
    GTEST_SKIP() << "Set MEBOOT_PERF=1 to run the selected GPU benchmark";

  const auto route = SelectedRoute();
  const char *name = route == BootRoute::kMEBoot ? "joint4434" : "ordinary";
  const auto profile = Profile::Selected(route);
  auto param = profile.MakeParameter();
  const auto prepare_start = std::chrono::steady_clock::now();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->memory_pool_.EnableStatistics();
  context->Prepare();
  const double prepare_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - prepare_start)
                                .count();

  UserInterface<uint64_t> client(context);
  EvkRequest request;
  context->AddRequiredRotations(request, Profile::kSlots);
  const auto keygen_start = std::chrono::steady_clock::now();
  client.PrepareRotationKey(request);
  if (route == BootRoute::kMEBoot)
    client.PrepareMEBootAKSKeys(context->GetMEBootCtS(Profile::kSlots));
  const double keygen_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - keygen_start)
                               .count();
  const auto prepared_memory = context->memory_pool_.GetStatistics();
  std::cout << std::setprecision(12) << "MEBOOT_PERF_META," << name << ','
            << Profile::kSlots << ',' << Profile::kInputLevel << ','
            << prepare_ms << ',' << keygen_ms << ','
            << prepared_memory.live_bytes << ',' << prepared_memory.peak_bytes
            << '\n';

  std::vector<Complex> message(Profile::kSlots);
  for (int i = 0; i < Profile::kSlots; ++i) {
    message[i] = Complex(0.2 * std::sin(i * 0.013),
                         0.2 * std::cos(i * 0.017));
  }
  Ciphertext<uint64_t> input, output;
  Plaintext<uint64_t> encoded;
  context->encoder_.Encode(encoded, Profile::kInputLevel,
                           Profile::kApplicationScale, message);
  client.Encrypt(input, encoded);

  const int warmups = EnvInt("MEBOOT_PERF_WARMUPS", 5);
  const int repeats = EnvInt("MEBOOT_PERF_REPEATS", 30);
  for (int i = 0; i < warmups; ++i) {
    context->Boot(output, input, client.GetEvkMap());
  }
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  cudaEvent_t start, end;
  ASSERT_EQ(cudaEventCreate(&start), cudaSuccess);
  ASSERT_EQ(cudaEventCreate(&end), cudaSuccess);
  for (int i = 0; i < repeats; ++i) {
    ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
    const auto host_start = std::chrono::steady_clock::now();
    ASSERT_EQ(cudaEventRecord(start), cudaSuccess);
    context->Boot(output, input, client.GetEvkMap());
    ASSERT_EQ(cudaEventRecord(end), cudaSuccess);
    ASSERT_EQ(cudaEventSynchronize(end), cudaSuccess);
    float gpu_ms = 0;
    ASSERT_EQ(cudaEventElapsedTime(&gpu_ms, start, end), cudaSuccess);
    const double wall_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - host_start)
                               .count();
    std::cout << std::setprecision(12) << "MEBOOT_PERF_SAMPLE,"
              << name << ',' << i << ',' << wall_ms << ',' << gpu_ms << '\n';
  }
  cudaEventDestroy(start);
  cudaEventDestroy(end);
  std::cout << std::setprecision(12) << "MEBOOT_PERF_OUTPUT," << name << ','
            << param->NPToLevel(output.GetNP()) << ','
            << std::log2(output.GetScale()) << ',' << output.GetNumSlots() << '\n';
  EXPECT_EQ(param->NPToLevel(output.GetNP()), profile.OutputLevel());
  if (route == BootRoute::kMEBoot) EXPECT_EQ(param->NPToLevel(output.GetNP()), 21);

  if (std::getenv("MEBOOT_PERF_PROFILE")) {
    Ciphertext<uint64_t> profiled;
    const int profile_repeats = EnvInt("MEBOOT_PERF_PROFILE_REPEATS", 3);
    for (int i = 0; i < warmups; ++i)
      context->ProfileBoot(profiled, input, client.GetEvkMap());
    for (int i = 0; i < profile_repeats; ++i) {
      const auto result =
          context->ProfileBoot(profiled, input, client.GetEvkMap());
      double stage_sum = 0;
      for (const auto &stage : result.stages) {
        stage_sum += stage.gpu_ms;
        std::cout << std::setprecision(12) << "MEBOOT_PERF_STAGE," << name
                  << ',' << i << ',' << stage.name << ',' << stage.gpu_ms << ','
                  << stage.host_submission_ms << '\n';
      }
      std::cout << std::setprecision(12) << "MEBOOT_PERF_TOTAL," << name
                << ',' << i << ',' << result.wall_ms << ',' << result.gpu_ms
                << ',' << stage_sum << '\n';
    }
  }
}

}  // namespace
