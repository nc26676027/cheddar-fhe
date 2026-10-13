#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <vector>

#include "common/Basic.cuh"
#include "common/PrimeUtils.h"
#include "core/DeviceVector.h"

using namespace cheddar;

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
void CheckCenteredBoundaries(const char *parameter_file) {
  std::ifstream file(std::string(PARAM_DIR) + "/" + parameter_file);
  ASSERT_TRUE(file.is_open());
  auto parameter = nlohmann::json::parse(file);
  HostVector<word> primes;
  for (int i = 0; i < 3; ++i) primes.push_back(parameter["main_primes"][i].get<word>());
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
