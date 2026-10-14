#pragma once

#include <cmath>
#include "core/Type.h"

namespace cheddar::basic {

// Six sub-61-bit primes and their six cofactors fit in 384 bits, including
// the signed sum. This exact path resolves the rare half-integer ambiguity
// left by the floating estimate; an odd modulus product has no exact tie.
__device__ __forceinline__ int ExactCenteredCRTQuotient(
    const int64_t *residues, int count, const uint64_t *product,
    const uint64_t *cofactors) {
  uint64_t numerator[6]{};
  for (int i = 0; i < count; ++i) {
    uint64_t term[6];
    const bool negative = residues[i] < 0;
    const uint64_t magnitude = negative ? uint64_t(-residues[i]) : uint64_t(residues[i]);
    uint128_t carry = 0;
    for (int j = 0; j < 6; ++j) {
      const uint128_t value = uint128_t(cofactors[6 * i + j]) * magnitude + carry;
      term[j] = uint64_t(value);
      carry = value >> 64;
    }
    if (negative) {
      carry = 1;
      for (int j = 0; j < 6; ++j) {
        const uint128_t value = uint128_t(~term[j]) + carry;
        term[j] = uint64_t(value);
        carry = value >> 64;
      }
    }
    carry = 0;
    for (int j = 0; j < 6; ++j) {
      const uint128_t value = uint128_t(numerator[j]) + term[j] + carry;
      numerator[j] = uint64_t(value);
      carry = value >> 64;
    }
  }
  const bool negative = numerator[5] >> 63;
  uint128_t carry = negative ? 1 : 0;
  if (negative) {
    for (int j = 0; j < 6; ++j) {
      const uint128_t value = uint128_t(~numerator[j]) + carry;
      numerator[j] = uint64_t(value);
      carry = value >> 64;
    }
  }
  carry = 0;
  for (int j = 0; j < 6; ++j) {
    const uint128_t value = (uint128_t(numerator[j]) << 1) + carry;
    numerator[j] = uint64_t(value);
    carry = value >> 64;
  }
  int quotient = 0;
  for (int k = 0; k < 3; ++k) {
    uint64_t threshold[6];
    carry = 0;
    for (int j = 0; j < 6; ++j) {
      const uint128_t value = uint128_t(product[j]) * (2 * k + 1) + carry;
      threshold[j] = uint64_t(value);
      carry = value >> 64;
    }
    int comparison = 0;
    for (int j = 5; j >= 0; --j) {
      if (numerator[j] == threshold[j]) continue;
      comparison = numerator[j] > threshold[j] ? 1 : -1;
      break;
    }
    if (comparison < 0) break;
    quotient = k + 1;
  }
  return negative ? -quotient : quotient;
}

__device__ __forceinline__ int CenteredCRTQuotient(
    const int64_t *residues, int count, const double *reciprocals,
    const uint64_t *product, const uint64_t *cofactors) {
  double estimate = 0;
  for (int i = 0; i < count; ++i) estimate += double(residues[i]) * reciprocals[i];
  int quotient = __double2int_rn(estimate);
  // Each |r_i/p_i| <= 1/2. Conversion, reciprocal, multiplication and
  // six-term addition incur less than 64 binary64 epsilons in total.
  if (fabs(estimate - quotient) >= 0.5 - 0x1p-46)
    return ExactCenteredCRTQuotient(residues, count, product, cofactors);
  return quotient;
}

}  // namespace cheddar::basic
