#include "Testbed.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>

namespace {

using Profile = Identity16Profile;

Identity16Route SelectedRoute() {
  const char *value = std::getenv("IDENTITY16_PERF_ROUTE");
  if (value && std::string(value) == "ordinary")
    return Identity16Route::kOrdinary;
  return Identity16Route::kJoint4434;
}

int EnvInt(const char *name, int fallback) {
  const char *value = std::getenv(name);
  return value ? std::max(1, std::atoi(value)) : fallback;
}

TEST(Identity16Selected, PublicBootstrapPerformance) {
  if (!std::getenv("IDENTITY16_PERF"))
    GTEST_SKIP() << "Set IDENTITY16_PERF=1 to run the selected GPU benchmark";

  const auto route = SelectedRoute();
  const char *name = route == Identity16Route::kJoint4434 ? "joint4434" : "ordinary";
  const auto profile = Profile::Selected(route);
  auto param = profile.MakeParameter();
  const auto prepare_start = std::chrono::steady_clock::now();
  auto context = BootContext<uint64_t>::CreateSelected(*param, route);
  context->memory_pool_.EnableStatistics();
  context->PrepareSelectedIdentity16();
  const double prepare_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - prepare_start)
                                .count();

  UserInterface<uint64_t> client(context);
  EvkRequest request;
  context->AddRequiredRotations(request, Profile::kSlots);
  const auto keygen_start = std::chrono::steady_clock::now();
  client.PrepareRotationKey(request);
  if (route == Identity16Route::kJoint4434)
    client.PrepareMEBootAKSKeys(context->GetSelectedExecution().GetJointCtS());
  const double keygen_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - keygen_start)
                               .count();
  const auto prepared_memory = context->memory_pool_.GetStatistics();
  std::cout << std::setprecision(12) << "IDENTITY16_PERF_META," << name << ','
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

  const int warmups = EnvInt("IDENTITY16_PERF_WARMUPS", 5);
  const int repeats = EnvInt("IDENTITY16_PERF_REPEATS", 30);
  for (int i = 0; i < warmups; ++i) {
    context->Boot(output, input, client.GetEvkMap(), false);
  }
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  cudaEvent_t start, end;
  ASSERT_EQ(cudaEventCreate(&start), cudaSuccess);
  ASSERT_EQ(cudaEventCreate(&end), cudaSuccess);
  for (int i = 0; i < repeats; ++i) {
    ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
    const auto host_start = std::chrono::steady_clock::now();
    ASSERT_EQ(cudaEventRecord(start), cudaSuccess);
    context->Boot(output, input, client.GetEvkMap(), false);
    ASSERT_EQ(cudaEventRecord(end), cudaSuccess);
    ASSERT_EQ(cudaEventSynchronize(end), cudaSuccess);
    float gpu_ms = 0;
    ASSERT_EQ(cudaEventElapsedTime(&gpu_ms, start, end), cudaSuccess);
    const double wall_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - host_start)
                               .count();
    std::cout << std::setprecision(12) << "IDENTITY16_PERF_SAMPLE,"
              << name << ',' << i << ',' << wall_ms << ',' << gpu_ms << '\n';
  }
  cudaEventDestroy(start);
  cudaEventDestroy(end);
  std::cout << std::setprecision(12) << "IDENTITY16_PERF_OUTPUT," << name << ','
            << param->NPToLevel(output.GetNP()) << ','
            << std::log2(output.GetScale()) << ',' << output.GetNumSlots() << '\n';

  if (std::getenv("IDENTITY16_PERF_PROFILE")) {
    Ciphertext<uint64_t> profiled;
    const int profile_repeats = EnvInt("IDENTITY16_PERF_PROFILE_REPEATS", 3);
    for (int i = 0; i < warmups; ++i)
      context->ProfileBoot(profiled, input, client.GetEvkMap(), false);
    for (int i = 0; i < profile_repeats; ++i) {
      const auto result =
          context->ProfileBoot(profiled, input, client.GetEvkMap(), false);
      double stage_sum = 0;
      for (const auto &stage : result.stages) {
        stage_sum += stage.gpu_ms;
        std::cout << std::setprecision(12) << "IDENTITY16_PERF_STAGE," << name
                  << ',' << i << ',' << stage.name << ',' << stage.gpu_ms << ','
                  << stage.host_submission_ms << '\n';
      }
      std::cout << std::setprecision(12) << "IDENTITY16_PERF_TOTAL," << name
                << ',' << i << ',' << result.wall_ms << ',' << result.gpu_ms
                << ',' << stage_sum << '\n';
    }
  }
}

}  // namespace
