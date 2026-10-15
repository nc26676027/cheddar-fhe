#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <vector>
#include <cstring>

#include "common/Basic.cuh"
#include "common/CenteredCRTQuotient.cuh"
#include "common/PrimeUtils.h"
#include "core/BigInt.h"
#include "core/Context.h"
#include "core/DeviceVector.h"
#include "extension/BootParameter.h"

using namespace cheddar;

__global__ void QuotientCases(int *result, const int64_t *residues, int count,
                              const double *reciprocals, const uint64_t *product,
                              const uint64_t *cofactors) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) return;
  result[2 * i] = basic::CenteredCRTQuotient(residues + 6 * i, 6, reciprocals, product, cofactors);
  result[2 * i + 1] = basic::ExactCenteredCRTQuotient(residues + 6 * i, 6, product, cofactors);
}

TEST(CenteredCRT, MEBootQuotientBoundaries) {
  for (const char *name : {"identity19_meboot.json", "ordinary.json"}) {
    std::ifstream file(std::string(PARAM_DIR) + "/" + name);
    ASSERT_TRUE(file.good());
    const auto profile = nlohmann::json::parse(file);
    for (int level : {21, 23, 28}) {
      std::vector<uint64_t> primes{profile["q"][level].get<uint64_t>()};
      for (const auto &prime : profile["p"]) primes.push_back(prime.get<uint64_t>());
      ASSERT_EQ(primes.size(), 6);
      BigInt product(uint64_t(1)), half(uint64_t(0));
      for (auto prime : primes) {
        BigInt factor{prime};
        BigInt::Mult(product, product, factor);
      }
      BigInt::Div2(half, product);
      const BigInt base(std::ldexp(1.0, 64));
      auto export_words = [&](BigInt number) {
        HostVector<uint64_t> words(6);
        for (int i = 0; i < 6; ++i) {
          words[i] = number.GetUnsigned();
          BigInt next(uint64_t(0));
          BigInt::Div(next, number, base);
          number = next;
        }
        return words;
      };
      HostVector<uint64_t> cofactors;
      HostVector<uint64_t> reciprocals;
      std::vector<BigInt> factors;
      std::vector<uint64_t> inverses;
      for (auto prime : primes) {
        BigInt divisor{prime}, factor(uint64_t(0)), residue(uint64_t(0));
        BigInt::Div(factor, product, divisor);
        factors.push_back(factor);
        auto words = export_words(factor);
        cofactors.insert(cofactors.end(), words.begin(), words.end());
        BigInt::Mod(residue, factor, divisor);
        inverses.push_back(primeutil::InvMod(residue.GetUnsigned(), prime));
        const double reciprocal = 1.0 / double(prime);
        uint64_t bits;
        std::memcpy(&bits, &reciprocal, sizeof(bits));
        reciprocals.push_back(bits);
      }
      HostVector<int64_t> residues;
      std::vector<int> expected;
      uint64_t state = 0x6330f64b178b6215ULL;
      for (int sample = 0; sample < 150; ++sample) {
        BigInt value(uint64_t(0));
        if (sample < 9) {
          value = half;
          BigInt offset{uint64_t(std::abs(sample - 4))};
          if (sample < 4) BigInt::Sub(value, value, offset);
          else BigInt::Add(value, value, offset);
        } else {
          for (int j = 0; j < 6; ++j) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            BigInt digit{state};
            BigInt::Mult(value, value, base);
            BigInt::Add(value, value, digit);
          }
        }
        BigInt::Mod(value, value, product);
        BigInt numerator(uint64_t(0));
        for (int j = 0; j < 6; ++j) {
          BigInt divisor{primes[j]}, residue(uint64_t(0));
          BigInt::Mod(residue, value, divisor);
          const uint64_t ordinary = primeutil::MultMod(residue.GetUnsigned(), inverses[j], primes[j]);
          const int64_t centered = ordinary > primes[j] / 2
              ? -int64_t(primes[j] - ordinary) : int64_t(ordinary);
          residues.push_back(centered);
          BigInt multiplier{uint64_t(centered < 0 ? -centered : centered)}, term(uint64_t(0));
          BigInt::Mult(term, multiplier, factors[j]);
          if (centered < 0) BigInt::Sub(numerator, numerator, term);
          else BigInt::Add(numerator, numerator, term);
        }
        BigInt remainder(uint64_t(0)), quotient(uint64_t(0));
        BigInt::NormalizeMod(remainder, numerator, product, half);
        BigInt::Sub(quotient, numerator, remainder);
        BigInt::Div(quotient, quotient, product);
        expected.push_back(int(quotient.GetDouble()));
      }
      DeviceVector<int64_t> residues_d;
      DeviceVector<uint64_t> reciprocals_d;
      DeviceVector<uint64_t> product_d, cofactors_d;
      DeviceVector<int> result_d(2 * expected.size());
      CopyHostToDevice(residues_d, residues);
      CopyHostToDevice(reciprocals_d, reciprocals);
      CopyHostToDevice(product_d, export_words(product));
      CopyHostToDevice(cofactors_d, cofactors);
      QuotientCases<<<1, 256>>>(result_d.data(), residues_d.data(), expected.size(),
          reinterpret_cast<const double *>(reciprocals_d.data()),
          product_d.data(), cofactors_d.data());
      HostVector<int> result;
      CopyDeviceToHost(result, result_d);
      for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(result[2 * i], expected[i]) << name << ',' << level << ',' << i;
        EXPECT_EQ(result[2 * i + 1], expected[i]) << name << ',' << level << ',' << i;
      }
    }
  }
}

TEST(CenteredCRT, MEBootRescaleExactResidues) {
  for (auto route : {BootRoute::kMEBoot, BootRoute::kOrdinary}) {
    auto profile = BootParameter::Selected(route);
    auto param = profile.MakeParameter();
    auto context = Context<uint64_t>::Create(*param);
    context->EnableCenteredMergedRescale();
    for (int level : {1, 20, 23, 28, 31}) {
      SCOPED_TRACE(level);
      const auto source_np = param->LevelToNP(level, 5);
      const auto target_np = param->LevelToNP(level - 1);
      const auto primes = param->GetPrimeVector(source_np);
      BigInt divisor(uint64_t(1)), half(uint64_t(0));
      for (int j = source_np.GetNumQ() - 1; j < source_np.GetNumTotal(); ++j) {
        BigInt prime{primes[j]};
        BigInt::Mult(divisor, divisor, prime);
      }
      BigInt::Div2(half, divisor);
      std::vector<BigInt> values, quotients;
      for (int sample = 0; sample < 18; ++sample) {
        BigInt remainder = half;
        BigInt offset{uint64_t(sample % 9)};
        BigInt::Add(remainder, remainder, offset);
        if (sample >= 9) BigInt::Neg(remainder, remainder);
        BigInt quotient{uint64_t(17 + sample)}, value(uint64_t(0));
        BigInt::Mult(value, quotient, divisor);
        BigInt::Add(value, value, remainder);
        BigInt centered(uint64_t(0));
        BigInt::NormalizeMod(centered, value, divisor, half);
        BigInt::Sub(quotient, value, centered);
        BigInt::Div(quotient, quotient, divisor);
        values.push_back(value);
        quotients.push_back(quotient);
      }
      HostVector<uint64_t> coefficients(source_np.GetNumTotal() * param->degree_);
      HostVector<uint64_t> expected(target_np.GetNumQ() * values.size());
      for (int j = 0; j < source_np.GetNumTotal(); ++j) {
        BigInt prime{primes[j]}, residue(uint64_t(0));
        for (size_t sample = 0; sample < values.size(); ++sample) {
          BigInt::Mod(residue, values[sample], prime);
          const uint64_t input = residue.GetUnsigned();
          for (int i = sample; i < param->degree_; i += values.size())
            coefficients[j * param->degree_ + i] = input;
          if (j < target_np.GetNumQ()) {
            BigInt::Mod(residue, quotients[sample], prime);
            expected[j * values.size() + sample] = residue.GetUnsigned();
          }
        }
      }
      DeviceVector<uint64_t> source, result(target_np.GetNumQ() * param->degree_);
      CopyHostToDevice(source, coefficients);
      auto source_view = source.View(5 * param->degree_);
      context->ntt_handler_.NTT(source_view, source_np,
                               source.ConstView(5 * param->degree_), true);
      auto result_view = result.View();
      context->mod_switch_handlers_.at(level).ModDownAndRescale(
          result_view, source.ConstView(5 * param->degree_));
      context->ntt_handler_.INTT(result_view, target_np, result.ConstView());
      HostVector<uint64_t> actual;
      CopyDeviceToHost(actual, result);
      for (int j = 0; j < target_np.GetNumQ(); ++j) {
        for (int i = 0; i < param->degree_; ++i) {
          ASSERT_EQ(actual[j * param->degree_ + i] % primes[j],
                    expected[j * values.size() + i % values.size()])
              << "route=" << int(route) << ",limb=" << j << ",coefficient=" << i;
        }
      }
    }
  }
}

TEST(CenteredCRT, Identity19CenteredModUpExactResidues) {
  for (auto route : {BootRoute::kMEBoot, BootRoute::kOrdinary}) {
    auto profile = BootParameter::Selected(route);
    auto param = profile.MakeParameter();
    auto context = Context<uint64_t>::Create(*param);
    context->EnableCenteredModUp();
    for (int level : {0, 1, 23, 31}) {
      SCOPED_TRACE(level);
      const auto source_np = param->LevelToNP(level);
      const auto target_np = param->LevelToNP(level, 5);
      const auto primes = param->GetPrimeVector(target_np);
      const auto &handler = context->mod_switch_handlers_.at(level);
      HostVector<uint64_t> source_coefficients(source_np.GetNumQ() * param->degree_);
      std::vector<std::vector<BigInt>> centered_values;
      for (int chunk = 0; chunk < handler.GetBeta(); ++chunk) {
        const int start = std::max(chunk * 5 - handler.GetPrimeOffset(), 0);
        const int end = std::min((chunk + 1) * 5, handler.GetPaddedNumQ()) -
                        handler.GetPrimeOffset();
        BigInt product(uint64_t(1)), half(uint64_t(0));
        for (int j = start; j < end; ++j) {
          BigInt prime{primes[j]};
          BigInt::Mult(product, product, prime);
        }
        BigInt::Div2(half, product);
        centered_values.emplace_back();
        for (int sample = 0; sample < 12; ++sample) {
          BigInt value = half, offset{uint64_t(sample % 6)};
          BigInt::Add(value, value, offset);
          if (sample >= 6) BigInt::Neg(value, value);
          for (int j = start; j < end; ++j) {
            BigInt prime{primes[j]}, residue(uint64_t(0));
            BigInt::Mod(residue, value, prime);
            for (int i = sample; i < param->degree_; i += 12)
              source_coefficients[j * param->degree_ + i] = residue.GetUnsigned();
          }
          BigInt centered(uint64_t(0));
          BigInt::NormalizeMod(centered, value, product, half);
          centered_values.back().push_back(centered);
        }
      }
      DeviceVector<uint64_t> source;
      CopyHostToDevice(source, source_coefficients);
      auto view = source.View();
      context->ntt_handler_.NTT(view, source_np, source.ConstView(), true);
      std::vector<DeviceVector<uint64_t>> raised;
      handler.ModUp(raised, source.ConstView());
      for (int chunk = 0; chunk < handler.GetBeta(); ++chunk) {
        auto destination = raised[chunk].View(5 * param->degree_);
        context->ntt_handler_.INTT(destination, target_np,
                                  raised[chunk].ConstView(5 * param->degree_));
        HostVector<uint64_t> actual;
        CopyDeviceToHost(actual, raised[chunk]);
        for (int j = 0; j < target_np.GetNumTotal(); ++j) {
          uint64_t expected[12];
          BigInt prime{primes[j]}, residue(uint64_t(0));
          for (int sample = 0; sample < 12; ++sample) {
            BigInt::Mod(residue, centered_values[chunk][sample], prime);
            expected[sample] = residue.GetUnsigned();
          }
          for (int i = 0; i < param->degree_; ++i)
            ASSERT_EQ(actual[j * param->degree_ + i] % primes[j], expected[i % 12])
                << "chunk=" << chunk << ",limb=" << j << ",coefficient=" << i;
        }
      }
    }
  }
}

template <typename word>
__global__ void ReconstructCases(uint64_t *result, const word *residues,
                                 const word *primes, word inverse01,
                                 word inverse012, int limbs, int count) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) return;
  bool negative;
  uint128_t magnitude;
  basic::ReconstructCenteredCRT<word>(negative, magnitude,
      residues[3 * i], residues[3 * i + 1], residues[3 * i + 2],
      primes, inverse01, inverse012, limbs);
  result[3 * i] = negative;
  result[3 * i + 1] = static_cast<uint64_t>(magnitude);
  result[3 * i + 2] = static_cast<uint64_t>(magnitude >> 64);
}

template <typename word>
void CheckCenteredBoundaries(const char *parameter_file, const char *prime_field = "main_primes") {
  std::ifstream file(std::string(PARAM_DIR) + "/" + parameter_file);
  ASSERT_TRUE(file.is_open());
  auto parameter = nlohmann::json::parse(file);
  HostVector<word> primes;
  for (int i = 0; i < 3; ++i) primes.push_back(parameter[prime_field][i].get<word>());
  word inverse01 = primeutil::InvMod<word>(primes[0] % primes[1], primes[1]);
  word inverse012 = primeutil::InvMod<word>(
      primeutil::MultMod<word>(primes[0] % primes[2], primes[1] % primes[2], primes[2]),
      primes[2]);
  DeviceVector<word> device_primes(3);
  CopyHostToDevice(device_primes, primes);

  for (int limbs = 1; limbs <= 3; ++limbs) {
    SCOPED_TRACE(limbs);
    uint128_t modulus = primes[0];
    int used = std::min(limbs, sizeof(word) == 4 ? 3 : 2);
    for (int i = 1; i < used; ++i) modulus *= primes[i];
    int128_t half = static_cast<int128_t>(modulus >> 1);
    std::vector<int128_t> values{0, 1, -1, half, half - 1, -half, -half + 1};
    for (int i = 0; i < 17; ++i) {
      values.push_back(static_cast<int128_t>((modulus / 19) * i) - half);
    }
    if (used > 1) {
      values.push_back(primes[0]);
      values.push_back(-static_cast<int128_t>(primes[0]));
    }
    HostVector<word> residues(values.size() * 3);
    for (size_t i = 0; i < values.size(); ++i) {
      for (int j = 0; j < 3; ++j) {
        int128_t remainder = values[i] % static_cast<int128_t>(primes[j]);
        if (remainder < 0) remainder += primes[j];
        residues[3 * i + j] = static_cast<word>(remainder);
      }
    }
    DeviceVector<word> device_residues(residues.size());
    DeviceVector<uint64_t> result(values.size() * 3);
    CopyHostToDevice(device_residues, residues);
    ReconstructCases<word><<<1, 64>>>(result.data(), device_residues.data(),
        device_primes.data(), inverse01, inverse012, limbs, values.size());
    ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
    HostVector<uint64_t> obtained;
    CopyDeviceToHost(obtained, result);
    for (size_t i = 0; i < values.size(); ++i) {
      SCOPED_TRACE(i);
      uint128_t magnitude = values[i] < 0 ? -values[i] : values[i];
      EXPECT_EQ(obtained[3 * i], values[i] < 0);
      EXPECT_EQ(obtained[3 * i + 1], static_cast<uint64_t>(magnitude));
      EXPECT_EQ(obtained[3 * i + 2], static_cast<uint64_t>(magnitude >> 64));
    }
  }
}

TEST(CenteredCRT, Word32Boundaries) { CheckCenteredBoundaries<uint32_t>("bootparam_40.json"); }
TEST(CenteredCRT, Word64Boundaries) { CheckCenteredBoundaries<uint64_t>("bootparam_40_64bit.json"); }
TEST(CenteredCRT, Identity19Boundaries) {
  CheckCenteredBoundaries<uint64_t>("identity19_meboot.json", "q");
}
