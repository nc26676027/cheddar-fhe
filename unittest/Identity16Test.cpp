#include "Testbed.h"
#include "extension/Identity16Mod1.h"
#include "extension/Identity16Transforms.h"

#include <limits>
#include <fstream>
#include <nlohmann/json.hpp>
#include "extension/MEBootMatrixPlanner.h"

namespace {
using Profile = Identity16Profile;

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
  for (int i = 0; i < 3; ++i) {
    root *= root;
    result = 2.0 * result * result - root;
  }
  return result;
}

TEST(Identity16Selected, CoefficientRoundingContract) {
  auto profile = Profile::Selected(Identity16Route::kJoint4434);
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

TEST(Identity16Selected, EncoderPolicyBeforePreparation) {
  for (auto route : {Identity16Route::kJoint4434, Identity16Route::kOrdinary}) {
    auto profile = Profile::Selected(route);
    auto param = profile.MakeParameter();
    auto context = BootContext<uint64_t>::CreateSelected(*param, route);
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

TEST(Identity16Selected, IndependentMatrixOracle) {
  const char *path = std::getenv("IDENTITY16_STAGE_ORACLE");
  if (!path) GTEST_SKIP() << "Set IDENTITY16_STAGE_ORACLE to the independent Go export";
  std::ifstream stream(path);
  ASSERT_TRUE(stream.good());
  nlohmann::json oracle;
  stream >> oracle;
  ASSERT_EQ(oracle.at("reference_revision"),
            "6330f64b178b6215edd35baf5b11c8da558ca95c");
  auto profile = Profile::Selected(Identity16Route::kOrdinary);
  auto param = profile.MakeParameter();
  auto context = Context<uint64_t>::Create(*param);
  const auto cts = MEBootMatrixPlanner::GenerateElementaryCtSStages<uint64_t>(context, Profile::kSlots);
  const auto stc = MEBootMatrixPlanner::GenerateElementaryStCStages<uint64_t>(context, Profile::kSlots);
  for (auto route : {Identity16Route::kJoint4434, Identity16Route::kOrdinary}) {
    profile = Profile::Selected(route);
    const char *name = route == Identity16Route::kJoint4434 ? "joint4434" : "ordinary";
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

TEST(Identity16Selected, ExactContracts) {
  for (auto route : {Identity16Route::kJoint4434, Identity16Route::kOrdinary}) {
    auto profile = Profile::Selected(route);
    auto param = profile.MakeParameter();
    EXPECT_EQ(param->GetPrimeVector(param->LevelToNP(31)), profile.q);
    EXPECT_EQ(param->aux_primes_, profile.p);
    EXPECT_EQ(param->max_key_switch_level_, 31);
    EXPECT_EQ(param->GetDenseHammingWeight(), 2048);
    EXPECT_EQ(profile.mod1_level - int(std::ceil(std::log2(profile.coefficients.size()))) - 3, 20);
    EXPECT_NEAR(profile.CtSGain() * profile.k * profile.q_diff, 1, 1e-15);
    EXPECT_NEAR(profile.q_diff,
                double(profile.q[0]) / std::exp2(std::round(std::log2(double(profile.q[0])))), 1e-15);
    if (route == Identity16Route::kJoint4434) {
      EXPECT_EQ(param->GetPrimeVector(param->LevelToNP(32))[0], profile.extra_q);
      EXPECT_EQ(profile.coefficients.size(), 63);
      EXPECT_EQ(profile.extension, 8);
      EXPECT_EQ(profile.sparse_h, 40);
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
      EXPECT_EQ(profile.coefficients.size(), 31);
      EXPECT_EQ(profile.sparse_h, 32);
      EXPECT_FALSE(profile.stc_output_log_scale);
      EXPECT_FALSE(profile.stc_intermediate_log_scale);
    }
  }
}

TEST(Identity16Selected, NativeEncryptedMod1) {
  for (auto route : {Identity16Route::kJoint4434, Identity16Route::kOrdinary}) {
    auto profile = Profile::Selected(route);
    auto param = profile.MakeParameter();
    auto context = Context<uint64_t>::Create(*param);
    context->encoder_.EnableNearestRounding();
    context->EnableCenteredMergedRescale();
    if (std::getenv("IDENTITY16_CENTERED_MODUP")) context->EnableCenteredModUp();
    Identity16Mod1 mod1(context, profile);
    // Mod1 consumes dense inputs only; subring entry is tested separately.
    UserInterface<uint64_t> client(
        context, route == Identity16Route::kJoint4434
                     ? SparseSecretLayout::kSubringPrefix : SparseSecretLayout::kFullRing,
        profile.extension, 3.2);
    ASSERT_EQ(mod1.GetDegree(), route == Identity16Route::kJoint4434 ? 62 : 30);
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
                << (route == Identity16Route::kJoint4434 ? "joint4434" : "ordinary")
                << ',' << stage << ',' << param->NPToLevel(ct.GetNP()) << ','
                << std::log2(ct.GetScale()) << ',' << error << '\n';
    };
    mod1.Evaluate(output, input, client.GetMultiplicationKey(), observe_mod1);
    client.Decrypt(decrypted, output);
    std::vector<Complex> obtained;
    context->encoder_.Decode(obtained, decrypted);
    ASSERT_EQ(obtained.size(), expected.size());
    EXPECT_EQ(param->NPToLevel(output.GetNP()), 20);
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
              << (route == Identity16Route::kJoint4434 ? "joint4434" : "ordinary")
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
              << (route == Identity16Route::kJoint4434 ? "joint4434" : "ordinary")
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
              << (route == Identity16Route::kJoint4434 ? "joint4434" : "ordinary")
              << ',' << max_error << '\n';
    if (!std::getenv("IDENTITY16_CENTERED_MODUP")) {
      context->EnableCenteredModUp();
      root = profile.sqrt2pi;
      mod1.Evaluate(output, input, client.GetMultiplicationKey(), observe_mod1);
      client.Decrypt(decrypted, output);
      context->encoder_.Decode(obtained, decrypted);
      max_error = 0;
      for (size_t i = 0; i < obtained.size(); ++i)
        max_error = std::max(max_error, std::abs(obtained[i] - expected[i]));
      std::cout << "SELECTED_MOD1_CENTERED_MODUP,"
                << (route == Identity16Route::kJoint4434 ? "joint4434" : "ordinary")
                << ',' << max_error << std::endl;
    }
  }
}

void RefreshDiagnostic(Identity16Route route) {
  auto profile = Profile::Selected(route);
  const char *name = route == Identity16Route::kJoint4434 ? "joint4434" : "ordinary";
  auto param = profile.MakeParameter();
  auto context = BootContext<uint64_t>::CreateSelected(*param, profile.route);
  context->memory_pool_.EnableStatistics();
  std::cout << "SELECTED_PREPARE_BEGIN," << name << std::endl;
  context->PrepareSelectedIdentity16();
  if (std::getenv("IDENTITY16_CENTERED_MODUP")) context->EnableCenteredModUp();
  context->memory_pool_.Trim();
  std::cout << "SELECTED_PREPARE_END," << name << std::endl;
  if (route == Identity16Route::kJoint4434) {
    std::cout << "SELECTED_LCR_BOUND,"
              << context->GetSelectedExecution().GetLCRInputBound() << ','
              << context->GetSelectedExecution().GetLCRProjectionBound() << std::endl;
  }
  const auto &transforms = context->GetSelectedExecution().GetTransforms();
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
  const uint64_t aks_bytes = route == Identity16Route::kJoint4434 ?
      uint64_t(31) * 2 * 37 * 65536 * 8 : 0;
  ASSERT_LT(rotation_bytes + common_bytes + aks_bytes + (uint64_t(1) << 30), free_bytes)
      << "Exact selected native keys and scratch exceed available GPU memory";
  UserInterface<uint64_t> client(context);
  client.PrepareRotationKey(request);
  if (route == Identity16Route::kJoint4434) {
    client.PrepareMEBootAKSKeys(context->GetSelectedExecution().GetJointCtS());
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
  const char *diagnostic_pattern = std::getenv("IDENTITY16_DIAGNOSTIC_PATTERN");
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
  if (std::getenv("IDENTITY16_DECODED_STAGES")) {
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
    if (std::getenv("IDENTITY16_DECODED_STAGES")) {
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
  context->GetSelectedExecution().Evaluate(output, input, client.GetEvkMap(), {}, observe);
  if (const char *path = std::getenv("IDENTITY16_DECODED_STAGES")) {
    std::ofstream stream(path);
    stream << stage_values;
  }
  Ciphertext<uint64_t> public_output;
  context->Boot(public_output, input, client.GetEvkMap(), false);
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
  EXPECT_EQ(param->NPToLevel(output.GetNP()), 20);
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
    context->Boot(output, input, client.GetEvkMap(), false);
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
    EXPECT_EQ(param->NPToLevel(output.GetNP()), 20);
    EXPECT_DOUBLE_EQ(output.GetScale(), Profile::kApplicationScale);
    EXPECT_LE(max_error, std::exp2(-18)) << pattern;
  }
  const auto executed_memory = context->memory_pool_.GetStatistics();
  std::cout << "SELECTED_MEMORY_EXECUTED," << executed_memory.live_bytes << ','
            << executed_memory.peak_bytes << ',' << executed_memory.allocation_calls << std::endl;
}

TEST(Identity16Selected, OrdinaryRefreshDiagnostic) {
  RefreshDiagnostic(Identity16Route::kOrdinary);
}
TEST(Identity16Selected, JointRefreshDiagnostic) {
  RefreshDiagnostic(Identity16Route::kJoint4434);
}

TEST(Identity16Selected, EightTermPrefixExactResidues) {
  auto profile = Profile::Selected(Identity16Route::kJoint4434);
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
