#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/BigInt.h"
#include "core/Container.h"
#include "core/NTT.h"
#include "core/Parameter.h"

namespace cheddar {

using Quad = __float128;

struct QuadComplex {
  Quad real = 0;
  Quad imag = 0;

  QuadComplex operator+(const QuadComplex &other) const;
  QuadComplex operator-(const QuadComplex &other) const;
  QuadComplex operator*(const QuadComplex &other) const;
  QuadComplex operator/(Quad divisor) const;
};

// Host-only, opt-in preprocessing. The caller owns param/ntt for this object's
// lifetime. Plaintext/Constant still store only double scale metadata: retain
// the precise scale for Decode and account for its rounding in later operations.
// This helper alone does not establish 81-bit end-to-end CKKS precision.
class HighPrecisionEncoding {
 public:
  HighPrecisionEncoding(const Parameter<uint64_t> &param,
                        const NTTHandler<uint64_t> &ntt);

  static Quad ParseDecimal(std::string_view value);
  static BigInt RoundToBigInt(Quad value);
  static Quad BigIntToQuad(const BigInt &value);

  void Encode(Plaintext<uint64_t> &ptxt, int level, Quad scale,
              const std::vector<QuadComplex> &message, int num_aux = 0) const;
  void Decode(std::vector<QuadComplex> &message,
              const Plaintext<uint64_t> &ptxt, Quad precise_scale) const;
  void EncodeConstant(Constant<uint64_t> &constant, int level, Quad scale,
                      Quad number, int num_aux = 0) const;
  // The integer is the already-scaled coefficient. No float conversion occurs.
  void EncodeConstantInteger(Constant<uint64_t> &constant, int level,
                             Quad scale, const BigInt &integer,
                             int num_aux = 0) const;

 private:
  void SpecialIFFT(std::vector<QuadComplex> &data) const;
  void SpecialFFT(std::vector<QuadComplex> &data) const;

  const Parameter<uint64_t> &param_;
  const NTTHandler<uint64_t> &ntt_;
  std::vector<QuadComplex> twiddles_;
};

}  // namespace cheddar
