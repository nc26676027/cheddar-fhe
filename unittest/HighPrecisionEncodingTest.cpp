#include <gtest/gtest.h>
#include <quadmath.h>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

#include "common/PrimeUtils.h"
#include "core/Context.h"
#include "extension/HighPrecisionEncoding.h"

namespace cheddar {
namespace {

constexpr uint64_t kQ0 = 4398044938241;
constexpr uint64_t kQ1 = 8590458881;
constexpr uint64_t kQ2 = 135135233;

std::unique_ptr<Parameter<uint64_t>> TestParameter() {
  return std::make_unique<Parameter<uint64_t>>(
      12, std::ldexp(1.0, 40), 1,
      std::vector<std::pair<int, int>>{{1, 0}, {2, 0}, {3, 0}},
      std::vector<uint64_t>{kQ0, kQ1, kQ2},
      std::vector<uint64_t>{288230376167047169});
}

TEST(HighPrecisionEncodingTest, DecimalIntegerAndSignedRounding) {
  const BigInt large("2417851639229258349424697");  // 2^81 + 12345
  EXPECT_EQ(large.ToString(), "2417851639229258349424697");
  EXPECT_EQ(HighPrecisionEncoding::RoundToBigInt(
                HighPrecisionEncoding::ParseDecimal(large.ToString())).ToString(),
            large.ToString());
  EXPECT_EQ(HighPrecisionEncoding::RoundToBigInt(
                HighPrecisionEncoding::ParseDecimal("0.5")).ToString(), "1");
  EXPECT_EQ(HighPrecisionEncoding::RoundToBigInt(
                HighPrecisionEncoding::ParseDecimal("-0.5")).ToString(), "-1");
  EXPECT_EQ(HighPrecisionEncoding::RoundToBigInt(
                HighPrecisionEncoding::ParseDecimal("-1.49")).ToString(), "-1");
  EXPECT_THROW(BigInt("12x"), std::invalid_argument);
  EXPECT_THROW(BigInt(""), std::invalid_argument);
  EXPECT_THROW(BigInt("+12"), std::invalid_argument);
  EXPECT_THROW(HighPrecisionEncoding::ParseDecimal("nan"), std::invalid_argument);
  EXPECT_THROW(HighPrecisionEncoding::ParseDecimal("1junk"), std::invalid_argument);
}

TEST(HighPrecisionEncodingTest, ExactIntegerConstantAndCoefficientResidues) {
  auto param = TestParameter();
  auto context = Context<uint64_t>::Create(*param);
  HighPrecisionEncoding hp(*param, context->ntt_handler_);
  const BigInt large("2417851639229258349424697");
  const BigInt negative("-2417851639229258349424697");
  Constant<uint64_t> constant;
  hp.EncodeConstantInteger(constant, 2, scalbnq(Quad(1), 81), negative);
  HostVector<uint64_t> encoded_constant;
  CopyDeviceToHost(encoded_constant, constant.cx_);
  ASSERT_EQ(cudaStreamSynchronize(cudaStreamLegacy), cudaSuccess);
  const auto primes = param->GetPrimeVector(param->LevelToNP(2));
  BigInt residue(uint64_t(0));
  for (size_t j = 0; j < primes.size(); ++j) {
    BigInt modulus(primes[j]);
    BigInt::Mod(residue, negative, modulus);
    EXPECT_EQ(encoded_constant[j],
              primeutil::ToMontgomery(residue.GetUnsigned(), primes[j]));
  }
  hp.EncodeConstant(constant, 2, Quad(1),
                    HighPrecisionEncoding::ParseDecimal(large.ToString()));
  CopyDeviceToHost(encoded_constant, constant.cx_);
  ASSERT_EQ(cudaStreamSynchronize(cudaStreamLegacy), cudaSuccess);
  for (size_t j = 0; j < primes.size(); ++j) {
    BigInt modulus(primes[j]);
    BigInt::Mod(residue, large, modulus);
    EXPECT_EQ(encoded_constant[j],
              primeutil::ToMontgomery(residue.GetUnsigned(), primes[j]));
  }

  Plaintext<uint64_t> plaintext;
  hp.Encode(plaintext, 2, Quad(1),
            {{HighPrecisionEncoding::BigIntToQuad(large), Quad(0)}});
  DeviceVector<uint64_t> coefficients(plaintext.mx_.size());
  auto view = coefficients.View();
  context->ntt_handler_.INTT(view, plaintext.GetNP(), plaintext.ConstView());
  HostVector<uint64_t> actual;
  CopyDeviceToHost(actual, coefficients);
  ASSERT_EQ(cudaStreamSynchronize(cudaStreamLegacy), cudaSuccess);
  for (size_t j = 0; j < primes.size(); ++j) {
    BigInt modulus(primes[j]);
    BigInt::Mod(residue, large, modulus);
    EXPECT_EQ(actual[j * param->degree_], residue.GetUnsigned());
  }

  std::vector<QuadComplex> decoded;
  hp.Decode(decoded, plaintext, Quad(1));
  ASSERT_EQ(decoded.size(), 1u);
  EXPECT_EQ(HighPrecisionEncoding::RoundToBigInt(decoded[0].real).ToString(),
            large.ToString());
}

TEST(HighPrecisionEncodingTest, ManySlotRoundTrip) {
  auto param = TestParameter();
  auto context = Context<uint64_t>::Create(*param);
  HighPrecisionEncoding hp(*param, context->ntt_handler_);
  const Quad scale = scalbnq(Quad(1), 81);
  std::vector<QuadComplex> input(2048);
  for (size_t i = 0; i < input.size(); ++i) {
    input[i] = {Quad(int(i % 17) - 8) / Quad(32),
                Quad(int(i % 11) - 5) / Quad(64)};
  }
  Plaintext<uint64_t> encoded;
  hp.Encode(encoded, 2, scale, input);
  std::vector<QuadComplex> output;
  hp.Decode(output, encoded, scale);
  ASSERT_EQ(output.size(), input.size());
  const Quad tolerance = scalbnq(Quad(1), -65);
  for (size_t i = 0; i < input.size(); ++i) {
    EXPECT_LT(fabsq(output[i].real - input[i].real), tolerance) << i;
    EXPECT_LT(fabsq(output[i].imag - input[i].imag), tolerance) << i;
  }
}

TEST(HighPrecisionEncodingTest, InvalidInputs) {
  auto param = TestParameter();
  auto context = Context<uint64_t>::Create(*param);
  HighPrecisionEncoding hp(*param, context->ntt_handler_);
  Plaintext<uint64_t> plaintext;
  EXPECT_THROW(hp.Encode(plaintext, 0, Quad(1), {}), std::invalid_argument);
  EXPECT_THROW(hp.Encode(plaintext, 0, Quad(0), {{Quad(1), Quad(0)}}),
               std::invalid_argument);
  EXPECT_THROW(hp.Encode(plaintext, 0, Quad(1),
                         std::vector<QuadComplex>(2049)),
               std::invalid_argument);
  Constant<uint64_t> constant;
  EXPECT_THROW(hp.EncodeConstant(constant, 0, Quad(1),
                                 HighPrecisionEncoding::ParseDecimal("inf")),
               std::invalid_argument);
  hp.Encode(plaintext, 0, Quad(16), {{Quad(1), Quad(0)}});
  std::vector<QuadComplex> output;
  EXPECT_THROW(hp.Decode(output, plaintext, Quad(8)), std::invalid_argument);
}

}  // namespace
}  // namespace cheddar
