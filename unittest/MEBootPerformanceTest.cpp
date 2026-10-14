#include "Testbed.h"

#include <cstdlib>
#include <limits>

namespace {

struct Precision {
  double max_error;
  double snr_db;
};

Precision MeasurePrecision(const std::vector<Complex> &expected,
                           const std::vector<Complex> &obtained) {
  double signal = 0, noise = 0, max_error = 0;
  for (size_t i = 0; i < expected.size(); ++i) {
    signal += std::norm(expected[i]);
    noise += std::norm(expected[i] - obtained[i]);
    max_error = std::max(max_error, std::abs(expected[i] - obtained[i]));
  }
  return {max_error, 10 * std::log10(signal / noise)};
}

template <typename word>
void PairedBootstrap(Testbed<word> &bed) {
  constexpr int slots = 1 << 15;
  auto standard = std::dynamic_pointer_cast<BootContext<word>>(bed.context_);
  auto meboot = BootContext<word>::Create(
      *bed.param_, BootParameter(bed.param_->max_level_,
                                 2, 3, 5, true,
                                 standard->boot_param_.num_double_angle_));
  // RMM allocations use the last installed context's pool. Standard matrices
  // and subsequently generated keys therefore depend on the MEBOOT pool's
  // lifetime. Release them before destroying its owner, including on failures.
  struct Cleanup {
    Testbed<word> &bed;
    std::shared_ptr<BootContext<word>> &standard;
    ~Cleanup() {
      bed.interface_.reset();
      standard.reset();
      bed.context_.reset();
    }
  } cleanup{bed, standard};
  standard->PrepareEvalMod();
  standard->PrepareEvalSpecialFFT(slots);
  meboot->PrepareEvalMod();
  meboot->PrepareEvalSpecialFFT(slots);

  // Both evaluators use the exact same main/subring secret and input ciphertext.
  EvkRequest req, standard_req, meboot_req;
  standard->AddRequiredRotations(standard_req, slots, false);
  meboot->AddRequiredRotations(meboot_req, slots, false);
  standard->AddRequiredRotations(req, slots, false);
  meboot->AddRequiredRotations(req, slots, false);
  bed.interface_->PrepareRotationKey(req);
  bed.interface_->PrepareMEBootAKSKeys(meboot->GetMEBootCtS(slots));
  const auto &keys = bed.interface_->GetEvkMap();
  const auto &cts = meboot->GetMEBootCtS(slots);
  const auto &param = *bed.param_;
  const int key_level = param.max_key_switch_level_;
  auto key_np = param.GetKeySwitchNP(key_level);
  EXPECT_EQ(standard->boot_param_.GetCtSStartLevel(), key_level);
  EXPECT_EQ(standard->boot_param_.num_cts_levels_, 4);
  EXPECT_EQ(standard->boot_param_.num_prefix_levels_, 0);
  EXPECT_EQ(meboot->boot_param_.num_cts_levels_, 2);
  EXPECT_EQ(meboot->boot_param_.num_prefix_levels_, 1);
  EXPECT_EQ(standard->boot_param_.GetEvalModStartLevel(), key_level - 4);
  EXPECT_EQ(cts.GetStage2Level(), key_level);
  EXPECT_EQ(cts.GetEndLevel(), key_level - 2);
  EXPECT_EQ(cts.GetStartLevel(), key_level + 1);
  EXPECT_FALSE(param.LevelToNP(cts.GetStartLevel()).IsSubsetOf(key_np));
  EXPECT_TRUE(param.LevelToNP(cts.GetStage2Level()).IsSubsetOf(key_np));
  for (const auto &[rotation, level] : req) {
    if (rotation != 0) EXPECT_TRUE(keys.GetRotationKey(rotation).GetNP().IsSubsetOf(key_np));
    EXPECT_LE(level, key_level);
  }
  for (const auto &[rotation, diagonal] : cts.GetStage2Matrix()) {
    EXPECT_TRUE(keys.GetAKSKey(rotation).GetNP().IsSubsetOf(key_np));
  }
  std::cout << "PERF_LEVELS," << sizeof(word) * 8 << ',' << key_level << ','
            << cts.GetStartLevel() << ',' << standard->boot_param_.GetEvalModStartLevel()
            << ',' << cts.GetEndLevel() << ',' << standard->boot_param_.GetEndLevel()
            << ',' << meboot->boot_param_.GetEndLevel() << '\n';
  auto key_bytes = [&](const EvaluationKey<word> &key) {
    return uint64_t(2) * key.GetBeta() * key.GetNP().GetNumTotal() *
           bed.param_->degree_ * sizeof(word);
  };
  uint64_t common_bytes = key_bytes(keys.GetMultiplicationKey()) +
                          key_bytes(keys.GetConjugationKey()) +
                          key_bytes(keys.GetDenseToSparseKey());
  uint64_t standard_bytes = common_bytes + key_bytes(keys.GetSparseToDenseKey());
  uint64_t meboot_bytes = common_bytes;
  for (const auto &[rotation, level] : standard_req) {
    if (rotation != 0) standard_bytes += key_bytes(keys.GetRotationKey(rotation));
  }
  for (const auto &[rotation, level] : meboot_req) {
    if (rotation != 0) meboot_bytes += key_bytes(keys.GetRotationKey(rotation));
  }
  for (const auto &[rotation, diagonal] : cts.GetStage2Matrix()) {
    meboot_bytes += key_bytes(keys.GetAKSKey(rotation));
  }
  std::cout << "PERF_KEYS," << sizeof(word) * 8 << ",standard," << standard_bytes
            << '\n' << "PERF_KEYS," << sizeof(word) * 8 << ",meboot," << meboot_bytes << '\n';
  for (size_t i = 0; i < cts.GetPlans().size(); ++i) {
    std::cout << "PERF_GAIN," << sizeof(word) * 8 << ',' << i + 1 << ','
              << std::abs(cts.GetPlans()[i].matrix.at(0)[0]) << '\n';
  }

  std::vector<Complex> message(slots);
  for (int j = 0; j < slots; ++j) {
    message[j] = Complex(0.7 * std::sin(j * 0.013),
                         0.7 * std::cos(j * 0.017));
  }
  Ciphertext<word> input, standard_out, meboot_out;
  bed.EncodeAndEncrypt(input, message, 0);
  for (int i = 0; i < 5; ++i) {
    standard->Boot(standard_out, input, keys, false);
    meboot->Boot(meboot_out, input, keys, false);
  }
  cudaDeviceSynchronize();

  int repeats = 3;
  if (const char *value = std::getenv("CHEDDAR_BENCH_REPEATS")) {
    repeats = std::max(1, std::atoi(value));
  }
  cudaEvent_t start, end;
  cudaEventCreate(&start);
  cudaEventCreate(&end);
  auto measure = [&](const char *label, const std::shared_ptr<BootContext<word>> &ctx,
                     Ciphertext<word> &output, int repeat) {
    cudaDeviceSynchronize();
    cudaEventRecord(start);
    auto begin = std::chrono::steady_clock::now();
    ctx->Boot(output, input, keys, false);
    cudaEventRecord(end);
    cudaEventSynchronize(end);
    double wall_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    float gpu_ms;
    cudaEventElapsedTime(&gpu_ms, start, end);
    std::cout << "PERF_SAMPLE," << sizeof(word) * 8 << ',' << label << ','
              << repeat << ',' << wall_ms << ',' << gpu_ms << '\n';
  };
  for (int repeat = 0; repeat < repeats; ++repeat) {
    if (repeat % 2 == 0) {
      measure("standard", standard, standard_out, repeat);
      measure("meboot", meboot, meboot_out, repeat);
    } else {
      measure("meboot", meboot, meboot_out, repeat);
      measure("standard", standard, standard_out, repeat);
    }
  }
  if (std::getenv("CHEDDAR_BOOT_PROFILE")) {
    Ciphertext<word> profile_standard_out, profile_meboot_out;
    auto profile = [&](const char *label, const auto &ctx, auto &output, int repeat) {
      auto result = ctx->ProfileBoot(output, input, keys, false);
      double sum = 0;
      for (const auto &stage : result.stages) {
        sum += stage.gpu_ms;
        EXPECT_GE(stage.gpu_ms, 0);
        std::cout << "BOOT_PROFILE_STAGE," << sizeof(word) * 8 << ',' << label
                  << ',' << repeat << ',' << stage.name << ',' << stage.gpu_ms
                  << ',' << stage.host_submission_ms << '\n';
      }
      EXPECT_NEAR(sum, result.gpu_ms, 0.02) << "Stage events must cover total interval";
      std::cout << "BOOT_PROFILE_TOTAL," << sizeof(word) * 8 << ',' << label
                << ',' << repeat << ',' << result.wall_ms << ',' << result.gpu_ms << '\n';
    };
    for (int warmup = 0; warmup < 5; ++warmup) {
      standard->ProfileBoot(profile_standard_out, input, keys, false);
      meboot->ProfileBoot(profile_meboot_out, input, keys, false);
    }
    for (int repeat = 0; repeat < repeats; ++repeat) {
      if (repeat % 2 == 0) {
        profile("standard", standard, profile_standard_out, repeat);
        profile("meboot", meboot, profile_meboot_out, repeat);
      } else {
        profile("meboot", meboot, profile_meboot_out, repeat);
        profile("standard", standard, profile_standard_out, repeat);
      }
    }
    auto compare = [&](const auto &reference, const auto &actual) {
      EXPECT_EQ(actual.GetNP(), reference.GetNP());
      EXPECT_EQ(actual.GetScale(), reference.GetScale());
      EXPECT_EQ(actual.GetNumSlots(), reference.GetNumSlots());
      HostVector<word> reference_b, reference_a, actual_b, actual_a;
      CopyDeviceToHost(reference_b, reference.bx_);
      CopyDeviceToHost(reference_a, reference.ax_);
      CopyDeviceToHost(actual_b, actual.bx_);
      CopyDeviceToHost(actual_a, actual.ax_);
      EXPECT_TRUE(reference_b == actual_b) << "Profiling changed Bx";
      EXPECT_TRUE(reference_a == actual_a) << "Profiling changed Ax";
    };
    compare(standard_out, profile_standard_out);
    compare(meboot_out, profile_meboot_out);
  }
  // Reproduce the real full-packed Boot input to CtS through public seams.
  double q0_product = 1;
  for (word prime : bed.param_->GetPrimeVector(bed.param_->LevelToNP(-1))) {
    q0_product *= prime;
  }
  int shift = static_cast<int>(std::log2(q0_product) + 0.5) -
              static_cast<int>(std::log2(bed.param_->base_scale_) + 0.5) - 5;
  Constant<word> scaleup;
  meboot->encoder_.EncodeConstant(scaleup, -1, 1.0, std::ldexp(1.0, shift));
  Ciphertext<word> scaled, raised, stage1, stage2, stage3, stage4;
  meboot->MultUnsafe(scaled, input, scaleup, -1);
  meboot->ModUpToMax(raised, scaled, keys);
  auto stage_time = [&](const char *label, auto evaluate) {
    cudaDeviceSynchronize();
    cudaEventRecord(start);
    auto begin = std::chrono::steady_clock::now();
    evaluate();
    cudaEventRecord(end);
    cudaEventSynchronize(end);
    double wall_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    float gpu_ms;
    cudaEventElapsedTime(&gpu_ms, start, end);
    std::cout << "PERF_STAGE," << sizeof(word) * 8 << ',' << label << ','
              << wall_ms << ',' << gpu_ms << '\n';
  };
  for (int warmup = 0; warmup < 5; ++warmup) {
    cts.EvaluateStage1(stage1, raised);
    cts.EvaluateStage2(stage2, stage1, keys);
    cts.EvaluateStage3(stage3, stage2, keys);
    cts.EvaluateStage4(stage4, stage3, keys);
  }
  for (int repeat = 0; repeat < repeats; ++repeat) {
    stage_time("M1", [&] { cts.EvaluateStage1(stage1, raised); });
    stage_time("M2", [&] { cts.EvaluateStage2(stage2, stage1, keys); });
    stage_time("M3", [&] { cts.EvaluateStage3(stage3, stage2, keys); });
    stage_time("M4", [&] { cts.EvaluateStage4(stage4, stage3, keys); });
  }
  cudaEventDestroy(start);
  cudaEventDestroy(end);

  EXPECT_EQ(bed.param_->NPToLevel(meboot_out.GetNP()),
            standard->boot_param_.GetEndLevel() + 2);
  std::vector<Complex> standard_message, meboot_message;
  bed.DecryptAndDecode(standard_message, standard_out);
  bed.DecryptAndDecode(meboot_message, meboot_out);
  ASSERT_EQ(standard_message.size(), message.size());
  ASSERT_EQ(meboot_message.size(), message.size());
  auto baseline = MeasurePrecision(message, standard_message);
  auto candidate = MeasurePrecision(message, meboot_message);
  const double precision_floor = sizeof(word) == 4 ? 128.0 : 114.0;
  EXPECT_GE(baseline.snr_db, precision_floor)
      << "Standard precision must remain at the accepted absolute baseline";
  EXPECT_GE(candidate.snr_db, precision_floor)
      << "MEBOOT must retain the accepted absolute precision";
  std::cout << std::setprecision(12)
            << "PERF_PRECISION," << sizeof(word) * 8 << ",standard,"
            << baseline.max_error << ',' << baseline.snr_db << '\n'
            << "PERF_PRECISION," << sizeof(word) * 8 << ",meboot,"
            << candidate.max_error << ',' << candidate.snr_db << '\n';
  EXPECT_TRUE(std::isfinite(baseline.snr_db));
  EXPECT_TRUE(std::isfinite(candidate.snr_db));
  EXPECT_GE(candidate.snr_db, baseline.snr_db - 1.0)
      << "MEBOOT must preserve standard precision (paired SNR alarm: 1 dB)";
  EXPECT_LT(candidate.max_error, 1e-3);
}

}  // namespace

TEST_P(SubringTestbed32, PairedMEBootPrecisionAndLatency) { PairedBootstrap(*this); }
TEST_P(SubringTestbed64, PairedMEBootPrecisionAndLatency) { PairedBootstrap(*this); }

INSTANTIATE_TEST_SUITE_P(Cheddar, SubringTestbed32,
                        testing::Values("bootparam_40.json"));
INSTANTIATE_TEST_SUITE_P(Cheddar, SubringTestbed64,
                        testing::Values("bootparam_40_64bit.json"));
class LowQ0PPerformance64 : public SubringTestbed64 {};
TEST_P(LowQ0PPerformance64, PairedMEBootPrecisionAndLatency) {
  PairedBootstrap(*this);
}
INSTANTIATE_TEST_SUITE_P(Experiment, LowQ0PPerformance64,
                        testing::Values("bootparam_40_64bit_lowp_entry.json"));
