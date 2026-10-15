#include "extension/HighPrecisionEncoding.h"

#include <quadmath.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "common/CommonUtils.h"
#include "common/PrimeUtils.h"

namespace cheddar {
namespace {

void CheckScale(Quad scale) {
  if (!finiteq(scale) || scale <= 0 ||
      !std::isfinite(static_cast<double>(scale)) ||
      static_cast<double>(scale) == 0)
    throw std::invalid_argument("precise scale cannot be stored in double metadata");
}

void CheckSlots(int slots, int max_slots) {
  if (slots <= 0 || slots > max_slots || (slots & (slots - 1)) != 0)
    throw std::invalid_argument("slot count must be a supported power of two");
}

std::string IntegerDecimal(Quad integer) {
  if (!finiteq(integer)) throw std::invalid_argument("nonfinite coefficient");
  char buffer[16384];
  const int length = quadmath_snprintf(buffer, sizeof(buffer), "%.0Qf", integer);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(buffer))
    throw std::overflow_error("binary128 integer formatting failed");
  return std::string(buffer, length);
}

}  // namespace

QuadComplex QuadComplex::operator+(const QuadComplex &other) const {
  return {real + other.real, imag + other.imag};
}

QuadComplex QuadComplex::operator-(const QuadComplex &other) const {
  return {real - other.real, imag - other.imag};
}

QuadComplex QuadComplex::operator*(const QuadComplex &other) const {
  return {real * other.real - imag * other.imag,
          real * other.imag + imag * other.real};
}

QuadComplex QuadComplex::operator/(Quad divisor) const {
  return {real / divisor, imag / divisor};
}

HighPrecisionEncoding::HighPrecisionEncoding(const Parameter<uint64_t> &param,
                                             const NTTHandler<uint64_t> &ntt)
    : param_(param), ntt_(ntt), twiddles_(2 * param.degree_) {
  const Quad pi = acosq(-1);
  const Quad m = Quad(2 * param.degree_);
  for (int i = 0; i < 2 * param.degree_; ++i) {
    const Quad angle = 2 * pi * Quad(i) / m;
    twiddles_[i] = {cosq(angle), sinq(angle)};
  }
}

Quad HighPrecisionEncoding::ParseDecimal(std::string_view value) {
  if (value.empty()) throw std::invalid_argument("empty binary128 decimal");
  const std::string input(value);
  char *end = nullptr;
  errno = 0;
  const Quad parsed = strtoflt128(input.c_str(), &end);
  if (end != input.c_str() + input.size() || end == input.c_str() ||
      errno == ERANGE || !finiteq(parsed))
    throw std::invalid_argument("invalid binary128 decimal");
  return parsed;
}

BigInt HighPrecisionEncoding::RoundToBigInt(Quad value) {
  if (!finiteq(value)) throw std::invalid_argument("nonfinite coefficient");
  return BigInt(IntegerDecimal(roundq(value)));
}

Quad HighPrecisionEncoding::BigIntToQuad(const BigInt &value) {
  return ParseDecimal(value.ToString());
}

void HighPrecisionEncoding::SpecialIFFT(std::vector<QuadComplex> &data) const {
  const int slots = data.size();
  CheckSlots(slots, param_.degree_ / 2);
  for (int stride = slots / 2; stride >= 1; stride /= 2) {
    const int st8 = stride << 3;
    const int gap = (2 * param_.degree_) / st8;
    for (int i = 0; i < slots; i += 2 * stride) {
      for (int j = 0; j < stride; ++j) {
        const int index = (st8 - (param_.GetGaloisFactor(j) % st8)) * gap;
        const auto x = data[i + j] + data[i + j + stride];
        const auto y = (data[i + j] - data[i + j + stride]) * twiddles_[index];
        data[i + j] = x;
        data[i + j + stride] = y;
      }
    }
  }
  BitReverseVector(data);
  for (auto &value : data) value = value / Quad(slots);
}

void HighPrecisionEncoding::SpecialFFT(std::vector<QuadComplex> &data) const {
  const int slots = data.size();
  CheckSlots(slots, param_.degree_ / 2);
  BitReverseVector(data);
  for (int stride = 1; stride < slots; stride *= 2) {
    const int st8 = stride << 3;
    const int gap = (2 * param_.degree_) / st8;
    for (int i = 0; i < slots; i += 2 * stride) {
      for (int j = 0; j < stride; ++j) {
        const int index = (param_.GetGaloisFactor(j) % st8) * gap;
        const auto x = data[i + j];
        const auto y = data[i + j + stride] * twiddles_[index];
        data[i + j] = x + y;
        data[i + j + stride] = x - y;
      }
    }
  }
}

void HighPrecisionEncoding::Encode(Plaintext<uint64_t> &ptxt, int level,
                                    Quad scale,
                                    const std::vector<QuadComplex> &message,
                                    int num_aux) const {
  CheckScale(scale);
  if (message.empty() || message.size() > size_t(param_.degree_ / 2))
    throw std::invalid_argument("invalid slot count");
  const int slots = 1 << Log2Ceil(int(message.size()));
  std::vector<QuadComplex> coefficients(slots);
  std::copy(message.begin(), message.end(), coefficients.begin());
  for (const auto &value : coefficients)
    if (!finiteq(value.real) || !finiteq(value.imag))
      throw std::invalid_argument("nonfinite slot value");
  SpecialIFFT(coefficients);

  const NPInfo np = param_.LevelToNP(level, num_aux);
  const auto primes = param_.GetPrimeVector(np);
  const int degree = param_.degree_;
  const int gap = (degree / 2) / slots;
  HostVector<uint64_t> encoded(primes.size() * degree, 0);
  std::vector<BigInt> big_primes;
  big_primes.reserve(primes.size());
  for (uint64_t prime : primes) big_primes.emplace_back(prime);
  for (int i = 0; i < slots; ++i) {
    const BigInt real = RoundToBigInt(coefficients[i].real * scale);
    const BigInt imag = RoundToBigInt(coefficients[i].imag * scale);
    BigInt residue(uint64_t(0));
    for (size_t j = 0; j < primes.size(); ++j) {
      BigInt::Mod(residue, real, big_primes[j]);
      encoded[j * degree + i * gap] = residue.GetUnsigned();
      BigInt::Mod(residue, imag, big_primes[j]);
      encoded[j * degree + i * gap + degree / 2] = residue.GetUnsigned();
    }
  }
  ptxt.ModifyNP(np);
  ptxt.SetNumSlots(slots);
  ptxt.SetScale(static_cast<double>(scale));
  CopyHostToDevice(ptxt.mx_, encoded);
  auto view = ptxt.View();
  ntt_.NTT(view, np, ptxt.ConstView(), true);
}

void HighPrecisionEncoding::EncodeConstantInteger(
    Constant<uint64_t> &constant, int level, Quad scale,
    const BigInt &integer, int num_aux) const {
  CheckScale(scale);
  const NPInfo np = param_.LevelToNP(level, num_aux);
  const auto primes = param_.GetPrimeVector(np);
  HostVector<uint64_t> encoded(primes.size());
  BigInt residue(uint64_t(0));
  for (size_t j = 0; j < primes.size(); ++j) {
    BigInt modulus(primes[j]);
    BigInt::Mod(residue, integer, modulus);
    encoded[j] = primeutil::ToMontgomery(residue.GetUnsigned(), primes[j]);
  }
  constant.ModifyNP(np);
  constant.SetScale(static_cast<double>(scale));
  CopyHostToDevice(constant.cx_, encoded);
}

void HighPrecisionEncoding::EncodeConstant(Constant<uint64_t> &constant,
                                            int level, Quad scale, Quad number,
                                            int num_aux) const {
  CheckScale(scale);
  if (!finiteq(number) || !finiteq(number * scale))
    throw std::invalid_argument("nonfinite scaled constant");
  EncodeConstantInteger(constant, level, scale, RoundToBigInt(number * scale),
                        num_aux);
}

void HighPrecisionEncoding::Decode(std::vector<QuadComplex> &message,
                                    const Plaintext<uint64_t> &ptxt,
                                    Quad precise_scale) const {
  CheckScale(precise_scale);
  if (static_cast<double>(precise_scale) != ptxt.GetScale())
    throw std::invalid_argument("precise scale disagrees with plaintext metadata");
  const NPInfo np = ptxt.GetNP();
  if (np.num_aux_ != 0) throw std::invalid_argument("decode requires Q-only plaintext");
  const int slots = ptxt.GetNumSlots();
  CheckSlots(slots, param_.degree_ / 2);
  const auto primes = param_.GetPrimeVector(np);
  const int degree = param_.degree_;
  HostVector<uint64_t> icrt1(primes.size());
  for (size_t i = 0; i < primes.size(); ++i) {
    uint64_t product = 1;
    for (size_t j = 0; j < primes.size(); ++j)
      if (i != j) product = primeutil::MultMod(product, primes[j], primes[i]);
    product = primeutil::MultMod(product, uint64_t(degree), primes[i]);
    icrt1[i] = primeutil::InvMod(product, primes[i]);
  }
  DeviceVector<uint64_t> inverse(icrt1.size());
  CopyHostToDevice(inverse, icrt1);
  Plaintext<uint64_t> adjusted(np);
  adjusted.SetNumSlots(slots);
  auto view = adjusted.View();
  ntt_.INTTAndMultConst(view, np, ptxt.ConstView(), inverse.ConstView());
  HostVector<uint64_t> host;
  CopyDeviceToHost(host, adjusted.mx_);
  if (cudaStreamSynchronize(cudaStreamLegacy) != cudaSuccess)
    throw std::runtime_error("high precision decode download failed");

  BigInt modulus(uint64_t(1));
  for (uint64_t prime : primes) {
    BigInt factor(prime);
    BigInt::Mult(modulus, modulus, factor);
  }
  BigInt half(uint64_t(0));
  BigInt::Div2(half, modulus);
  std::vector<BigInt> cofactors;
  cofactors.reserve(primes.size());
  for (uint64_t prime : primes) {
    BigInt factor(prime), quotient(uint64_t(0));
    BigInt::Div(quotient, modulus, factor);
    cofactors.push_back(quotient);
  }
  const int gap = (degree / 2) / slots;
  message.resize(slots);
  for (int i = 0; i < slots; ++i) {
    BigInt real(uint64_t(0)), imag(uint64_t(0)), term(uint64_t(0));
    for (size_t j = 0; j < primes.size(); ++j) {
      BigInt value(host[j * degree + i * gap]);
      BigInt::Mult(term, value, cofactors[j]);
      BigInt::Add(real, real, term);
      value.Set(host[j * degree + i * gap + degree / 2]);
      BigInt::Mult(term, value, cofactors[j]);
      BigInt::Add(imag, imag, term);
    }
    BigInt::NormalizeMod(real, real, modulus, half);
    BigInt::NormalizeMod(imag, imag, modulus, half);
    message[i] = {BigIntToQuad(real) / precise_scale,
                  BigIntToQuad(imag) / precise_scale};
  }
  SpecialFFT(message);
}

}  // namespace cheddar
