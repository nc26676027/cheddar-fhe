#include "Testbed.h"
#include "extension/BootContext.h"
#include "extension/MEBootEvalMod.h"
#include "extension/BootTransforms.h"

#include <limits>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <nlohmann/json.hpp>
#include <zlib.h>
#include "extension/MEBootMatrixPlanner.h"

namespace {
using Profile = BootParameter;

Complex PlainComplexMod1(const Profile &profile, Complex value) {
  const Complex x = value - 0.25 / profile.k;
  Complex t0 = 1, t1 = x;
  Complex result = profile.coefficients[0] + profile.coefficients[1] * x;
  for (size_t i = 2; i < profile.coefficients.size(); ++i) {
    const Complex t2 = 2.0 * x * t1 - t0;
    result += profile.coefficients[i] * t2;
    t0 = t1;
    t1 = t2;
  }
  double root = profile.sqrt2pi;
  for (int i = 0; i < profile.double_angle; ++i) {
    root *= root;
    result = 2.0 * result * result - root;
  }
  return result;
}

TEST(MEBootIdentity19, CoefficientRoundingContract) {
  auto profile = Profile::Selected(BootRoute::kMEBoot);
  auto param = profile.MakeParameter();
  auto context = Context<uint64_t>::Create(*param);
  for (bool nearest : {false, true}) {
    if (nearest) context->encoder_.EnableNearestRounding();
    for (double coefficient : {0.49, 0.5, -0.5, 1.5, -1.5, 2.49, -2.49}) {
      Plaintext<uint64_t> encoded;
      context->encoder_.Encode(encoded, 0, 16,
                              std::vector<Complex>(8, Complex(coefficient / 16,
                                                             -coefficient / 16)));
      DeviceVector<uint64_t> coefficients(encoded.mx_.size());
      auto view = coefficients.View();
      context->ntt_handler_.INTT(view, encoded.GetNP(), encoded.ConstView());
      HostVector<uint64_t> actual;
      CopyDeviceToHost(actual, coefficients);
      const int64_t rounded = int64_t(nearest ? std::round(coefficient) : coefficient);
      const uint64_t q = profile.q[0];
      const uint64_t real = rounded < 0 ? q - uint64_t(-rounded) : uint64_t(rounded);
      const uint64_t imag = rounded > 0 ? q - uint64_t(rounded) : uint64_t(-rounded);
      EXPECT_EQ(actual[0] % q, real);
      EXPECT_EQ(actual[param->degree_ / 2] % q, imag);
      for (int i = 1; i < param->degree_ / 2; ++i) {
        ASSERT_EQ(actual[i] % q, 0);
        ASSERT_EQ(actual[i + param->degree_ / 2] % q, 0);
      }
    }
  }
}

TEST(MEBootIdentity19, EncoderPolicyBeforePreparation) {
  for (auto route : {BootRoute::kMEBoot, BootRoute::kOrdinary}) {
    auto profile = Profile::Selected(route);
    auto param = profile.MakeParameter();
    auto context = BootContext<uint64_t>::Create(*param, profile);
    Plaintext<uint64_t> encoded;
    context->encoder_.Encode(encoded, 0, 16, std::vector<Complex>(8, 1.5 / 16));
    DeviceVector<uint64_t> coefficients(encoded.mx_.size());
    auto view = coefficients.View();
    context->ntt_handler_.INTT(view, encoded.GetNP(), encoded.ConstView());
    HostVector<uint64_t> actual;
    CopyDeviceToHost(actual, coefficients);
    EXPECT_EQ(actual[0] % profile.q[0], 2);
  }
}

TEST(MEBootIdentity19, IndependentMatrixOracle) {
  const char *override_path = std::getenv("MEBOOT_STAGE_ORACLE");
  const std::string path = override_path ? override_path :
      std::string(PARAM_DIR) + "/identity19-stage-oracle.json.gz";
  std::string contents;
  if (path.size() >= 3 && path.substr(path.size() - 3) == ".gz") {
    gzFile stream = gzopen(path.c_str(), "rb");
    ASSERT_NE(stream, nullptr) << path;
    char buffer[65536];
    int count;
    while ((count = gzread(stream, buffer, sizeof(buffer))) > 0)
      contents.append(buffer, count);
    const int close_status = gzclose(stream);
    ASSERT_EQ(count, 0) << "Failed to read oracle: " << path;
    ASSERT_EQ(close_status, Z_OK);
  } else {
    std::ifstream stream(path);
    ASSERT_TRUE(stream.good()) << path;
    contents.assign(std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>());
  }
  nlohmann::json oracle;
  ASSERT_NO_THROW(oracle = nlohmann::json::parse(contents));
  ASSERT_EQ(oracle.at("reference_revision"),
            "6330f64b178b6215edd35baf5b11c8da558ca95c");
  auto profile = Profile::Selected(BootRoute::kOrdinary);
  auto param = profile.MakeParameter();
  auto context = Context<uint64_t>::Create(*param);
  const auto cts = MEBootMatrixPlanner::GenerateElementaryCtSStages<uint64_t>(context, Profile::kSlots);
  const auto stc = MEBootMatrixPlanner::GenerateElementaryStCStages<uint64_t>(context, Profile::kSlots);
  for (auto route : {BootRoute::kMEBoot, BootRoute::kOrdinary}) {
    profile = Profile::Selected(route);
    const char *name = route == BootRoute::kMEBoot ? "joint4434" : "ordinary";
    for (const char *group : {"stc", "cts", "bypass"}) {
      const auto &expected = oracle.at(name).at(group);
      std::vector<int> widths;
      double gain;
      if (std::string(group) == "stc") {
        widths = {7, 8};
        gain = 1.0 / 64;
      } else if (std::string(group) == "cts") {
        widths = profile.widths;
        gain = profile.ProperCtSGain() / (2 * Profile::kSlots);
      } else {
        int remaining = 15;
        for (int i = 0; i < int(expected.size()); ++i) {
          int width = (remaining + int(expected.size()) - i - 1) / (int(expected.size()) - i);
          widths.push_back(width);
          remaining -= width;
        }
        gain = 0.25 / (2 * Profile::kSlots);
      }
      ASSERT_EQ(widths.size(), expected.size());
      const auto &elementary = std::string(group) == "stc" ? stc : cts;
      std::vector<Complex> values(Profile::kSlots);
      for (size_t i = 0; i < values.size(); ++i)
        values[i] = Complex((double(i % 33) / 32 - 0.5) / 2,
                            (double((i / 33) % 33) / 32 - 0.5) / 2);
      int cursor = 0;
      for (size_t factor = 0; factor < widths.size(); ++factor) {
        auto matrix = elementary.at(cursor++);
        for (int j = 1; j < widths[factor]; ++j)
          matrix = StripedMatrix::Mult(elementary.at(cursor++), matrix);
        matrix = StripedMatrix::Mult(matrix, std::pow(gain, 1.0 / widths.size()));
        double coefficient_error = 0, value_error = 0;
        for (const auto &sample : expected.at(factor).at("coefficient_samples")) {
          int rotation = sample.at(0), slot = sample.at(1);
          ASSERT_TRUE(matrix.count(rotation)) << name << ',' << group << ',' << factor;
          const double gain_ratio = std::string(group) == "cts" ?
              profile.ProperCtSGain() / profile.CtSGain() : 1;
          Complex value(sample.at(2).get<double>(), sample.at(3).get<double>());
          value *= std::pow(gain_ratio, 1.0 / widths.size());
          coefficient_error = std::max(coefficient_error, std::abs(matrix.at(rotation).at(slot) - value));
        }
        std::vector<Complex> next(values.size());
        for (const auto &[rotation, diagonal] : matrix)
          for (size_t j = 0; j < values.size(); ++j)
            next[j] += diagonal[j] * values[(j + rotation) % values.size()];
        for (size_t j = 0; j < values.size(); ++j) {
          const auto &value = expected.at(factor).at("signed_grid_values").at(j);
          const double gain_ratio = std::string(group) == "cts" ?
              profile.ProperCtSGain() / profile.CtSGain() : 1;
          value_error = std::max(value_error, std::abs(next[j] -
              std::pow(gain_ratio, double(factor + 1) / widths.size()) *
              Complex(value.at(0).get<double>(), value.at(1).get<double>())));
        }
        std::cout << std::setprecision(17) << "SELECTED_MATRIX_ORACLE," << name << ','
                  << group << ',' << factor << ',' << coefficient_error << ',' << value_error << '\n';
        EXPECT_LE(coefficient_error, 1e-12);
        EXPECT_LE(value_error, 1e-10);
        values = std::move(next);
      }
    }
  }
}

TEST(MEBootIdentity19, ExactContracts) {
  for (auto route : {BootRoute::kMEBoot, BootRoute::kOrdinary}) {
    auto profile = Profile::Selected(route);
    auto param = profile.MakeParameter();
    const auto filename = route == BootRoute::kMEBoot ?
        "identity19_meboot.json" : "ordinary.json";
    const auto source_dir = std::filesystem::path(__FILE__).parent_path().parent_path();
    std::ifstream stream(source_dir / "parameters" / filename);
    ASSERT_TRUE(stream.good());
    nlohmann::json exported;
    stream >> exported;
    EXPECT_EQ(exported["q"].get<std::vector<uint64_t>>(), profile.q);
    EXPECT_EQ(exported["p"].get<std::vector<uint64_t>>(), profile.p);
    EXPECT_EQ(exported["mod1"]["LevelQ"].get<int>(), profile.mod1_level);
    EXPECT_EQ(exported["mod1"]["K"].get<int>(), profile.k);
    EXPECT_EQ(exported["mod1"]["DoubleAngle"].get<int>(), profile.double_angle);
    EXPECT_DOUBLE_EQ(exported["mod1_sqrt2pi"].get<double>(), profile.sqrt2pi);
    ASSERT_EQ(exported["mod1_coefficients_float64"].size(), profile.coefficients.size());
    for (size_t i = 0; i < profile.coefficients.size(); ++i) {
      EXPECT_DOUBLE_EQ(exported["mod1_coefficients_float64"][i][0].get<double>(),
                       profile.coefficients[i]);
    }
    EXPECT_EQ(param->GetPrimeVector(param->LevelToNP(31)), profile.q);
    EXPECT_EQ(param->aux_primes_, profile.p);
    EXPECT_EQ(param->max_key_switch_level_, 31);
    EXPECT_EQ(param->GetDenseHammingWeight(), 2048);
    EXPECT_EQ(profile.mod1_level -
                  int(std::ceil(std::log2(profile.coefficients.size()))) -
                  profile.double_angle, profile.OutputLevel());
    EXPECT_EQ(exported["output_level"].get<int>(), profile.OutputLevel());
    EXPECT_NEAR(profile.CtSGain() * profile.k * profile.q_diff, 1, 1e-15);
    EXPECT_NEAR(profile.q_diff,
                double(profile.q[0]) / std::exp2(std::round(std::log2(double(profile.q[0])))), 1e-15);
    if (route == BootRoute::kMEBoot) {
      EXPECT_EQ(exported["proper_extra_q"].get<uint64_t>(), profile.extra_q);
      EXPECT_EQ(profile.k, 20);
      EXPECT_EQ(profile.double_angle, 2);
      EXPECT_EQ(profile.mod1_level, 29);
      EXPECT_EQ(profile.OutputLevel(), 21);
      EXPECT_EQ(param->GetPrimeVector(param->LevelToNP(32))[0], profile.extra_q);
      EXPECT_EQ(profile.coefficients.size(), 63);
      EXPECT_EQ(profile.extension, 8);
      EXPECT_EQ(profile.sparse_h, 40);
      EXPECT_EQ(param->degree_ / profile.extension, 8192);
      EXPECT_EQ(profile.q[0], 4398044938241ULL);
      EXPECT_EQ(param->entry_primes_, std::vector<uint64_t>{786433});
      EXPECT_DOUBLE_EQ(profile.interval_a, -5);
      EXPECT_DOUBLE_EQ(profile.interval_b, 5);
      const double pi = std::acos(-1.0);
      EXPECT_NEAR(std::pow(profile.sqrt2pi, 1 << profile.double_angle),
                  profile.q_diff * 0.25 * profile.Mod1ReturnGain() / (2 * pi),
                  1e-15);
      EXPECT_DOUBLE_EQ(*profile.stc_output_log_scale, 33);
      EXPECT_DOUBLE_EQ(profile.ScaleDownMultiplier(), 1);
      EXPECT_DOUBLE_EQ(profile.ModUpMultiplier(), 32768);
      EXPECT_DOUBLE_EQ(profile.Mod1ReturnGain(), 0.5);
      EXPECT_NEAR(profile.ProperCtSGain() * profile.ModUpMultiplier(),
                  profile.CtSGain() * 65536, 1e-12);
      // Encoder IFFT inverts StC: bound physical coefficients in input units.
      const double message_bound = std::sqrt(2.0) * 0.25 / 64;
      EXPECT_LT(message_bound * std::exp2(*profile.stc_output_log_scale),
                double(profile.q[0]) / 2);
    } else {
      EXPECT_EQ(profile.k, 16);
      EXPECT_EQ(profile.double_angle, 3);
      EXPECT_EQ(profile.OutputLevel(), 20);
      EXPECT_EQ(profile.coefficients.size(), 31);
      EXPECT_EQ(profile.sparse_h, 32);
      EXPECT_FALSE(profile.stc_output_log_scale);
      EXPECT_FALSE(profile.stc_intermediate_log_scale);
    }
  }
}

TEST(MEBootIdentity19, RejectMismatchedCoreParameters) {
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  auto profile = Profile::Selected(BootRoute::kMEBoot);
  auto wrong_prefix = profile;
  wrong_prefix.extra_q = Profile::Selected(BootRoute::kOrdinary).q[0];
  auto param = wrong_prefix.MakeParameter();
  EXPECT_EXIT({ BootContext<uint64_t>::Create(*param, profile); },
              testing::ExitedWithCode(EXIT_FAILURE),
              "Bootstrap modulus contract mismatch");

  Parameter<uint64_t> no_entry(
      16, Profile::kApplicationScale, profile.OutputLevel(),
      param->level_config_, profile.q, profile.p, {profile.extra_q}, {0, 0}, 31);
  no_entry.SetDenseHammingWeight(2048);
  no_entry.SetSparseHammingWeight(profile.sparse_h);
  EXPECT_EXIT({ BootContext<uint64_t>::Create(no_entry, profile); },
              testing::ExitedWithCode(EXIT_FAILURE),
              "Bootstrap modulus contract mismatch");
}

TEST(MEBootIdentity19, NativeEncryptedMod1) {
  for (auto route : {BootRoute::kMEBoot, BootRoute::kOrdinary}) {
    auto profile = Profile::Selected(route);
    auto param = profile.MakeParameter();
    auto context = Context<uint64_t>::Create(*param);
    context->encoder_.EnableNearestRounding();
    context->EnableCenteredMergedRescale();
    if (std::getenv("MEBOOT_CENTERED_MODUP")) context->EnableCenteredModUp();
    MEBootEvalMod mod1(context, profile);
    if (route == BootRoute::kMEBoot) {
      // Check the new polynomial against sine, independently of encrypted/plain parity.
      double approximation_error = 0;
      for (int integer = 1 - profile.k; integer < profile.k; ++integer) {
        for (double fraction : {-0.5 / 1024, 0.0, 0.5 / 1024}) {
          const double pi = std::acos(-1.0);
          const double amplitude = profile.q_diff * 0.25 * profile.Mod1ReturnGain() / (2 * pi);
          const double expected = amplitude * std::sin(2 * pi * fraction);
          const double actual = mod1.PlainEvaluate((integer + fraction) / profile.k);
          approximation_error = std::max(approximation_error, 1024 * std::abs(actual - expected));
        }
      }
      std::cout << "SELECTED_MOD1_APPROXIMATION,joint4434,"
                << approximation_error << std::endl;
      EXPECT_LE(approximation_error, std::exp2(-18));
    }
    // Mod1 consumes dense inputs only; subring entry is tested separately.
    UserInterface<uint64_t> client(
        context, route == BootRoute::kMEBoot
                     ? SparseSecretLayout::kSubringPrefix : SparseSecretLayout::kFullRing,
        profile.extension, 3.2);
    ASSERT_EQ(mod1.GetDegree(), route == BootRoute::kMEBoot ? 62 : 30);
    std::vector<Complex> values(Profile::kSlots), expected(Profile::kSlots);
    for (int i = 0; i < Profile::kSlots; ++i) {
      const int integer = (i % (2 * profile.k - 1)) - (profile.k - 1);
      const double fractional = (double((i / (2 * profile.k - 1)) % 17) / 16 - 0.5) / 1024;
      const double normalized = (integer + fractional) / profile.k;
      values[i] = normalized * 1024;
      expected[i] = mod1.PlainEvaluate(normalized) * 1024;
    }
    Plaintext<uint64_t> encoded, decrypted;
    Ciphertext<uint64_t> input, output;
    context->encoder_.Encode(encoded, profile.mod1_level, Profile::kModUpScale, values);
    client.Encrypt(input, encoded);
    std::vector<double> stage_expected(values.size());
    double root = profile.sqrt2pi;
    auto observe_mod1 = [&](int stage, const Ciphertext<uint64_t> &ct) {
      Plaintext<uint64_t> pt;
      client.Decrypt(pt, ct);
      std::vector<Complex> actual;
      context->encoder_.Decode(actual, pt);
      if (stage >= 2) root *= root;
      double error = 0;
      for (size_t i = 0; i < values.size(); ++i) {
        if (stage == 0) stage_expected[i] = values[i].real() / 1024 - .25 / profile.k;
        else if (stage == 1) stage_expected[i] = mod1.PlainPolynomial(values[i].real() / 1024);
        else stage_expected[i] = 2 * stage_expected[i] * stage_expected[i] - root;
        error = std::max(error, std::abs(actual[i] - stage_expected[i]));
      }
      std::cout << std::setprecision(17) << "SELECTED_MOD1_INTERNAL,"
                << (route == BootRoute::kMEBoot ? "joint4434" : "ordinary")
                << ',' << stage << ',' << param->NPToLevel(ct.GetNP()) << ','
                << std::log2(ct.GetScale()) << ',' << error << '\n';
    };
    mod1.Evaluate(output, input, client.GetMultiplicationKey(), observe_mod1);
    client.Decrypt(decrypted, output);
    std::vector<Complex> obtained;
    context->encoder_.Decode(obtained, decrypted);
    ASSERT_EQ(obtained.size(), expected.size());
    EXPECT_EQ(param->NPToLevel(output.GetNP()), profile.OutputLevel());
    EXPECT_DOUBLE_EQ(output.GetScale(), Profile::kModUpScale);
    double max_error = 0, signal = 0, noise = 0;
    for (size_t i = 0; i < obtained.size(); ++i) {
      ASSERT_TRUE(std::isfinite(obtained[i].real()) && std::isfinite(obtained[i].imag()));
      const double error = std::abs(obtained[i] - expected[i]);
      max_error = std::max(max_error, error);
      signal += std::norm(expected[i]);
      noise += std::norm(obtained[i] - expected[i]);
    }
    std::cout << std::setprecision(17) << "SELECTED_MOD1,"
              << (route == BootRoute::kMEBoot ? "joint4434" : "ordinary")
              << ',' << mod1.GetDegree() << ',' << max_error << ','
              << 10 * std::log10(signal / noise) << '\n';
    // This is an isolated polynomial arithmetic check, not refresh acceptance.
    EXPECT_LE(max_error, std::exp2(-18));
    context->SetMergedRelinearizeRescale(false);
    root = profile.sqrt2pi;
    mod1.Evaluate(output, input, client.GetMultiplicationKey(), observe_mod1);
    client.Decrypt(decrypted, output);
    context->encoder_.Decode(obtained, decrypted);
    max_error = 0;
    for (size_t i = 0; i < obtained.size(); ++i)
      max_error = std::max(max_error, std::abs(obtained[i] - expected[i]));
    std::cout << "SELECTED_MOD1_UNMERGED,"
              << (route == BootRoute::kMEBoot ? "joint4434" : "ordinary")
              << ',' << max_error << '\n';
    context->SetMergedRelinearizeRescale(true);
    // A zero-a ciphertext isolates coefficient/scheduling rounding from
    // encryption and evaluation-key noise with identical encoded slots.
    Ciphertext<uint64_t> transparent(input.GetNP()), transparent_output;
    transparent.SetNumSlots(Profile::kSlots);
    transparent.SetScale(input.GetScale());
    CopyDeviceToDevice(transparent.bx_, encoded.mx_);
    ASSERT_EQ(cudaMemset(transparent.ax_.data(), 0,
                         transparent.ax_.size() * sizeof(uint64_t)), cudaSuccess);
    root = profile.sqrt2pi;
    mod1.Evaluate(transparent_output, transparent, client.GetMultiplicationKey(), observe_mod1);
    client.Decrypt(decrypted, transparent_output);
    context->encoder_.Decode(obtained, decrypted);
    max_error = 0;
    for (size_t i = 0; i < obtained.size(); ++i)
      max_error = std::max(max_error, std::abs(obtained[i] - expected[i]));
    std::cout << "SELECTED_MOD1_TRANSPARENT,"
              << (route == BootRoute::kMEBoot ? "joint4434" : "ordinary")
              << ',' << max_error << '\n';
    if (!std::getenv("MEBOOT_CENTERED_MODUP")) {
      context->EnableCenteredModUp();
      root = profile.sqrt2pi;
      mod1.Evaluate(output, input, client.GetMultiplicationKey(), observe_mod1);
      client.Decrypt(decrypted, output);
      context->encoder_.Decode(obtained, decrypted);
      max_error = 0;
      for (size_t i = 0; i < obtained.size(); ++i)
        max_error = std::max(max_error, std::abs(obtained[i] - expected[i]));
      std::cout << "SELECTED_MOD1_CENTERED_MODUP,"
                << (route == BootRoute::kMEBoot ? "joint4434" : "ordinary")
                << ',' << max_error << std::endl;
    }
  }
}

void RefreshDiagnostic(BootRoute route) {
  auto profile = Profile::Selected(route);
  const char *name = route == BootRoute::kMEBoot ? "joint4434" : "ordinary";
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::Create(*param, profile);
  context->memory_pool_.EnableStatistics();
  std::cout << "SELECTED_PREPARE_BEGIN," << name << std::endl;
  context->Prepare();
  if (std::getenv("MEBOOT_CENTERED_MODUP")) context->EnableCenteredModUp();
  context->memory_pool_.Trim();
  std::cout << "SELECTED_PREPARE_END," << name << std::endl;
  if (route == BootRoute::kMEBoot) {
    std::cout << "SELECTED_LCR_BOUND,"
              << context->GetExecution().GetLCRInputBound() << ','
              << context->GetExecution().GetLCRProjectionBound() << std::endl;
  }
  const auto &transforms = context->GetExecution().GetTransforms();
  for (const auto *group : {&transforms.GetStC(), &transforms.GetCtS(), &transforms.GetBypass()}) {
    for (const auto &factor : *group) {
      std::cout << "SELECTED_TRANSFORM," << factor.GetLevel() << ',' << factor.GetBS()
                << ',' << factor.GetNumBabySteps() << ',' << factor.GetNumGiantSteps()
                << ',' << factor.GetPlaintextBytes() << ',' << factor.GetCompactBytes() << '\n';
    }
  }
  EvkRequest request;
  context->AddRequiredRotations(request, Profile::kSlots);
  uint64_t rotation_bytes = 0;
  for (const auto &[rotation, level] : request) {
    if (!rotation) continue;
    const auto np = param->GetKeySwitchNP(level);
    const auto bytes = uint64_t(2) * ((np.GetNumQ() + 4) / 5) * np.GetNumTotal() * 65536 * 8;
    rotation_bytes += bytes;
    std::cout << "SELECTED_KEY_PLAN," << rotation << ',' << level << ',' << bytes << '\n';
  }
  std::cout << "SELECTED_ROTATION_BYTES," << rotation_bytes << std::endl;
  size_t free_bytes, total_bytes;
  ASSERT_EQ(cudaMemGetInfo(&free_bytes, &total_bytes), cudaSuccess);
  const auto key_np = param->GetKeySwitchNP(31);
  const uint64_t common_bytes = uint64_t(3) * 2 * 7 * key_np.GetNumTotal() * 65536 * 8;
  std::cout << "SELECTED_PRE_KEY_MEMORY," << free_bytes << ',' << total_bytes
            << ',' << common_bytes << std::endl;
  const uint64_t aks_bytes = route == BootRoute::kMEBoot ?
      uint64_t(31) * 2 * 37 * 65536 * 8 : 0;
  ASSERT_LT(rotation_bytes + common_bytes + aks_bytes + (uint64_t(1) << 30), free_bytes)
      << "Exact native keys and prepared state exceed available GPU memory";
  UserInterface<uint64_t> client(context);
  client.PrepareRotationKey(request);
  if (route == BootRoute::kMEBoot) {
    client.PrepareMEBootAKSKeys(context->GetMEBootCtS(Profile::kSlots));
  }
  const auto prepared_memory = context->memory_pool_.GetStatistics();
  std::cout << "SELECTED_MEMORY_PREPARED," << prepared_memory.live_bytes << ','
            << prepared_memory.peak_bytes << ',' << prepared_memory.allocation_calls << std::endl;
  std::vector<Complex> values(Profile::kSlots);
  for (size_t i = 0; i < values.size(); ++i) {
    values[i] = Complex((double(i % 33) / 32 - 0.5) / 2,
                        (double((i / 33) % 33) / 32 - 0.5) / 2);
    switch (i % 7) {
      case 0: values[i] = Complex(0, 0); break;
      case 1: values[i] = Complex(0.25, 0); break;
      case 2: values[i] = Complex(-0.25, 0); break;
      case 3: values[i] = Complex(0, 0.25); break;
      case 4: values[i] = Complex(0, -0.25); break;
      case 5: values[i] = Complex(0.25, -0.25); break;
      default: break;
    }
  }
  const char *diagnostic_pattern = std::getenv("MEBOOT_DIAGNOSTIC_PATTERN");
  const bool single_pattern = diagnostic_pattern &&
      (std::string(diagnostic_pattern) == "endpoints" ||
       std::string(diagnostic_pattern) == "constant-mixed");
  if (single_pattern) {
    for (size_t i = 0; i < values.size(); ++i) {
      values[i] = std::string(diagnostic_pattern) == "constant-mixed" ?
          Complex(0.25, 0.25) :
          Complex(i % 2 ? -0.25 : 0.25, i % 3 ? 0.25 : -0.25);
    }
  }
  Ciphertext<uint64_t> input, output;
  Plaintext<uint64_t> encoded, decrypted;
  context->encoder_.Encode(encoded, 2, Profile::kApplicationScale, values);
  client.Encrypt(input, encoded);
  std::vector<Complex> mod1_input;
  nlohmann::json stage_values = nlohmann::json::array();
  if (std::getenv("MEBOOT_DECODED_STAGES")) {
    nlohmann::json record{{"stage", "input"}, {"level", 2},
                          {"scale", Profile::kApplicationScale},
                          {"values", nlohmann::json::array()}};
    for (auto value : values) record["values"].push_back({value.real(), value.imag()});
    stage_values.push_back(std::move(record));
  }
  auto observe = [&](const char *stage, const Ciphertext<uint64_t> &ct) {
    Plaintext<uint64_t> pt;
    if (std::string(stage) == "entry" || std::string(stage) == "proper" ||
        std::string(stage) == "m1") client.DecryptSparse(pt, ct);
    else client.Decrypt(pt, ct);
    std::vector<Complex> decoded;
    context->encoder_.Decode(decoded, pt);
    if (std::string(stage) == "pre-mod1") mod1_input = decoded;
    if (std::string(stage) == "mod1") {
      double error = 0;
      for (size_t i = 0; i < decoded.size(); ++i) {
        const Complex expected = 1024.0 * PlainComplexMod1(profile, mod1_input[i] / 1024.0);
        error = std::max(error, std::abs(decoded[i] - expected));
      }
      std::cout << "SELECTED_MOD1_ARITHMETIC_ERROR," << error << std::endl;
    }
    if (std::getenv("MEBOOT_DECODED_STAGES")) {
      nlohmann::json record{{"stage", stage}, {"level", param->NPToLevel(ct.GetNP())},
                            {"scale", ct.GetScale()}, {"values", nlohmann::json::array()}};
      for (auto value : decoded) record["values"].push_back({value.real(), value.imag()});
      stage_values.push_back(std::move(record));
    }
    double max_abs = 0;
    for (auto value : decoded) max_abs = std::max(max_abs, std::abs(value));
    std::cout << std::setprecision(17) << "SELECTED_STAGE," << stage << ','
              << param->NPToLevel(ct.GetNP()) << ',' << std::log2(ct.GetScale())
              << ',' << max_abs << ',' << decoded[0].real() << ',' << decoded[0].imag() << std::endl;
  };
  if (single_pattern) {
    Ciphertext<uint64_t> transparent(input.GetNP()), transformed;
    transparent.SetNumSlots(Profile::kSlots);
    transparent.SetScale(input.GetScale());
    CopyDeviceToDevice(transparent.bx_, encoded.mx_);
    ASSERT_EQ(cudaMemset(transparent.ax_.data(), 0,
                        transparent.ax_.size() * sizeof(uint64_t)), cudaSuccess);
    transforms.StC(transformed, transparent, client.GetEvkMap());
    observe("transparent-stc", transformed);
  }
  context->GetExecution().Evaluate(output, input, client.GetEvkMap(), {}, observe);
  if (const char *path = std::getenv("MEBOOT_DECODED_STAGES")) {
    std::ofstream stream(path);
    stream << stage_values;
  }
  Ciphertext<uint64_t> public_output;
  context->Boot(public_output, input, client.GetEvkMap());
  HostVector<uint64_t> observed_bx, observed_ax, public_bx, public_ax;
  CopyDeviceToHost(observed_bx, output.bx_);
  CopyDeviceToHost(observed_ax, output.ax_);
  CopyDeviceToHost(public_bx, public_output.bx_);
  CopyDeviceToHost(public_ax, public_output.ax_);
  EXPECT_TRUE(observed_bx == public_bx);
  EXPECT_TRUE(observed_ax == public_ax);
  EXPECT_EQ(output.GetNP(), public_output.GetNP());
  EXPECT_DOUBLE_EQ(output.GetScale(), public_output.GetScale());
  std::cout << "SELECTED_PUBLIC_EXACT," << name << ','
            << (observed_bx == public_bx && observed_ax == public_ax) << std::endl;
  client.Decrypt(decrypted, output);
  std::vector<Complex> actual;
  context->encoder_.Decode(actual, decrypted);
  ASSERT_EQ(actual.size(), values.size());
  double max_error = 0, noise = 0, signal = 0;
  for (size_t i = 0; i < actual.size(); ++i) {
    ASSERT_TRUE(std::isfinite(actual[i].real()) && std::isfinite(actual[i].imag()));
    max_error = std::max(max_error, std::abs(actual[i] - values[i]));
    noise += std::norm(actual[i] - values[i]);
    signal += std::norm(values[i]);
  }
  std::cout << std::setprecision(17) << "SELECTED_REFRESH," << name << ','
            << param->NPToLevel(output.GetNP()) << ',' << std::log2(output.GetScale())
            << ',' << max_error << ',' << 10 * std::log10(signal / noise) << '\n';
  EXPECT_EQ(param->NPToLevel(output.GetNP()), profile.OutputLevel());
  if (route == BootRoute::kMEBoot) EXPECT_EQ(param->NPToLevel(output.GetNP()), 21);
  EXPECT_DOUBLE_EQ(output.GetScale(), Profile::kApplicationScale);
  EXPECT_LE(max_error, std::exp2(-18));
  if (single_pattern) return;
  for (const char *pattern : {"signed-grid", "zeros", "endpoints", "independent"}) {
    const std::string pattern_name(pattern);
    for (size_t i = 0; i < values.size(); ++i) {
      const double x = -0.25 + 0.5 * double(i) / double(values.size() - 1);
      const double y = -0.25 + 0.5 * double((i * 1031) % values.size()) /
                                       double(values.size() - 1);
      if (pattern_name == "signed-grid") values[i] = Complex(x, -x);
      else if (pattern_name == "zeros") values[i] = Complex(0, 0);
      else if (pattern_name == "endpoints")
        values[i] = Complex(i % 2 ? -0.25 : 0.25, i % 3 ? 0.25 : -0.25);
      else values[i] = Complex(i % 3 ? x : 0, i % 5 ? y : 0);
    }
    context->encoder_.Encode(encoded, 2, Profile::kApplicationScale, values);
    client.Encrypt(input, encoded);
    context->Boot(output, input, client.GetEvkMap());
    client.Decrypt(decrypted, output);
    context->encoder_.Decode(actual, decrypted);
    ASSERT_EQ(actual.size(), values.size());
    max_error = noise = signal = 0;
    for (size_t i = 0; i < actual.size(); ++i) {
      ASSERT_TRUE(std::isfinite(actual[i].real()) && std::isfinite(actual[i].imag()));
      max_error = std::max(max_error, std::abs(actual[i] - values[i]));
      noise += std::norm(actual[i] - values[i]);
      signal += std::norm(values[i]);
    }
    std::cout << "SELECTED_PATTERN," << name << ',' << pattern << ','
              << param->NPToLevel(output.GetNP()) << ','
              << std::log2(output.GetScale()) << ',' << max_error << ','
              << (signal ? 10 * std::log10(signal / noise) :
                           -std::numeric_limits<double>::infinity()) << std::endl;
    EXPECT_EQ(param->NPToLevel(output.GetNP()), profile.OutputLevel());
    if (route == BootRoute::kMEBoot) EXPECT_EQ(param->NPToLevel(output.GetNP()), 21);
    EXPECT_DOUBLE_EQ(output.GetScale(), Profile::kApplicationScale);
    EXPECT_LE(max_error, std::exp2(-18)) << pattern;
  }
  const auto executed_memory = context->memory_pool_.GetStatistics();
  std::cout << "SELECTED_MEMORY_EXECUTED," << executed_memory.live_bytes << ','
            << executed_memory.peak_bytes << ',' << executed_memory.allocation_calls << std::endl;
}

TEST(MEBootIdentity19, OrdinaryRefreshDiagnostic) {
  RefreshDiagnostic(BootRoute::kOrdinary);
}
TEST(MEBootIdentity19, JointRefreshDiagnostic) {
  RefreshDiagnostic(BootRoute::kMEBoot);
}

TEST(MEBootIdentity19, EightTermPrefixExactResidues) {
  auto profile = Profile::Selected(BootRoute::kMEBoot);
  auto param = profile.MakeParameter();
  auto context = Context<uint64_t>::Create(*param);
  MEBootCtS<uint64_t> cts(context, Profile::kSlots, 32, profile.CtSGain() / 65536,
                         profile.widths, profile.bases, 8);
  Ciphertext<uint64_t> input(param->LevelToNP(32)), fused, control, accumulated;
  input.SetNumSlots(Profile::kSlots);
  input.SetScale(Profile::kModUpScale);
  const auto primes = param->GetPrimeVector(input.GetNP());
  HostVector<uint64_t> b(primes.size() * 65536), a(b.size());
  for (size_t limb = 0; limb < primes.size(); ++limb) {
    for (int i = 0; i < 65536; ++i) {
      b[limb * 65536 + i] = (uint64_t(i) * 6364136223846793005ULL) % primes[limb];
      a[limb * 65536 + i] = (uint64_t(i) * 1442695040888963407ULL) % primes[limb];
    }
  }
  CopyHostToDevice(input.bx_, b);
  CopyHostToDevice(input.ax_, a);
  cts.EvaluateStage1(fused, input);
  bool first = true;
  ASSERT_EQ(cts.GetPrefixDiagonals().size(), 8);
  for (const auto &[rotation, diagonal] : cts.GetPrefixDiagonals()) {
    EXPECT_EQ(param->GetGaloisFactor(rotation) % 16384, 1);
    Ciphertext<uint64_t> rotated, product;
    context->Permute(rotated, input, rotation);
    context->MultUnsafe(product, rotated, diagonal, 32);
    if (first) context->Copy(accumulated, product);
    else context->Add(accumulated, accumulated, product);
    first = false;
  }
  context->Rescale(control, accumulated);
  HostVector<uint64_t> fb, fa, cb, ca;
  CopyDeviceToHost(fb, fused.bx_);
  CopyDeviceToHost(fa, fused.ax_);
  CopyDeviceToHost(cb, control.bx_);
  CopyDeviceToHost(ca, control.ax_);
  EXPECT_TRUE(fb == cb);
  EXPECT_TRUE(fa == ca);
  EXPECT_EQ(fused.GetNP(), param->LevelToNP(31));
  EXPECT_EQ(fused.GetScale(), control.GetScale());
}
}  // namespace
