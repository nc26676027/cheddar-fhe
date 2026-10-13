#pragma once

#include "common/DoubleWord.h"
#include "core/Type.h"

namespace cheddar {
namespace basic {
namespace detail {

template <class... T>
constexpr bool always_false = false;

/**
 * @brief Wide multiplication.
 *
 * @tparam T                             uint32_t, int32_t, uint64_t, or int64_t
 * @param a                              any number
 * @param b                              any number
 * @return make_signed_double_word_t<T>  a * b
 */
template <typename T>
__inline__ __device__ make_double_word_t<T> __mult_wide(const T a, const T b) {
  static_assert(always_false<T>, "Must use correct template specialization");
  return 1;
}

template <>
__inline__ __device__ uint64_t __mult_wide<uint32_t>(const uint32_t a,
                                                     const uint32_t b) {
  uint64_t mult;
  asm("mul.wide.u32 %0, %1, %2;" : "=l"(mult) : "r"(a), "r"(b));
  return mult;
}

template <>
__inline__ __device__ int64_t __mult_wide<int32_t>(const int32_t a,
                                                   const int32_t b) {
  int64_t mult;
  asm("mul.wide.s32 %0, %1, %2;" : "=l"(mult) : "r"(a), "r"(b));
  return mult;
}

template <>
__inline__ __device__ uint128_t __mult_wide<uint64_t>(const uint64_t a,
                                                      const uint64_t b) {
  // TODO(jongmin.kim): better implement this part using 32-bit PTX
  uint64_t hi = __umul64hi(a, b);
  uint64_t lo = a * b;
  return (static_cast<uint128_t>(hi) << 64) | lo;
}

template <>
__inline__ __device__ int128_t __mult_wide<int64_t>(const int64_t a,
                                                    const int64_t b) {
  // TODO (jongmin.kim): better implement this part using 32-bit PTX
  int64_t hi = __mul64hi(a, b);
  uint64_t lo = a * b;
  return (static_cast<int128_t>(hi) << 64) | lo;
}

/**
 * @brief This Montgomery reduction implementation uses signed computation
 * to cover various ranged numbers. Returns signed numbers in (-q, q).
 * Example given in 2^32 word size.
 *
 * @tparam word                 either uint32_t or uint64_t
 * @param a                     any signed number in range [-q*2^31, q*2^31 - 1]
 * @param q                     an odd prime smaller than 2^31
 * @param q_inv                 q^-1 mod 2^32
 * @return make_signed_t<word>  output is in range [-(q-1), q-1]
 */
template <typename word>
__inline__ __device__ make_signed_t<word> __montgomery_reduction_lazy(
    const make_signed_double_word_t<word> a, const word q,
    const make_signed_t<word> q_inv) {
  static_assert(always_false<word>, "Must use correct template specialization");
  return 1;
}

template <>
__inline__ __device__ int32_t __montgomery_reduction_lazy<uint32_t>(
    const int64_t a, const uint32_t q, const int32_t q_inv) {
  int32_t lo, hi;
  asm("mov.b64 {%0, %1}, %2;" : "=r"(lo), "=r"(hi) : "l"(a));
  int32_t temp = lo * q_inv;
  temp = __mulhi(temp, static_cast<int32_t>(q));
  return hi - temp;
}

template <>
__inline__ __device__ int64_t __montgomery_reduction_lazy<uint64_t>(
    const int128_t a, const uint64_t q, const int64_t q_inv) {
  // TODO: better implement this part
  int64_t lo = static_cast<int64_t>(a);
  int64_t hi = static_cast<int64_t>(a >> 64);
  int64_t temp = lo * q_inv;
  temp = __mul64hi(temp, static_cast<int64_t>(q));
  return hi - temp;
}

/**
 * @brief Performs lazy Montgomery modular reduction (a * b) % q and returns
 * result in (-q, q). Example given in 2^32 word size.
 * a * b must be in range [-q*2^31, q*2^31 - 1].
 *
 * @tparam word                 either uint32_t or uint64_t
 * @param a                     any signed number
 * @param b                     any signed number
 * @param q                     an odd prime smaller than 2^31
 * @return make_signed_t<word>  output in range [-(q-1), q-1]
 */
template <typename word>
__inline__ __device__ make_signed_t<word> __mult_montgomery_lazy(
    const make_signed_t<word> a, const make_signed_t<word> b, const word q,
    const make_signed_t<word> q_inv) {
  static_assert(always_false<word>, "Must use correct template specialization");
  return 1;
}

template <>
__inline__ __device__ int32_t __mult_montgomery_lazy<uint32_t>(
    const int32_t a, const int32_t b, const uint32_t q, const int32_t q_inv) {
  int64_t mult = __mult_wide<int32_t>(a, b);
  return __montgomery_reduction_lazy<uint32_t>(mult, q, q_inv);
}

template <>
__inline__ __device__ int64_t __mult_montgomery_lazy<uint64_t>(
    const int64_t a, const int64_t b, const uint64_t q, const int64_t q_inv) {
  // TODO (jongmin.kim): better implement this part
  // hopefully, compiler can do the job for you
  int64_t hi = __mul64hi(a, b);
  int64_t lo = a * b;
  int64_t temp = lo * q_inv;
  temp = __mul64hi(temp, static_cast<int64_t>(q));
  return hi - temp;
}

}  // namespace detail

/**
 * @brief Returns Montgomery reduced number in [0, q)
 *
 * @tparam word  either uint32_t or uint64_t
 * @param a      any signed number in range [-q*2^31, q*2^31 - 1]
 * @param q      an odd prime smaller than 2^31
 * @param q_inv  q^-1 mod 2^32
 * @return word  output is in range [0, q-1]
 */
template <typename word>
__inline__ __device__ word
ReduceMontgomery(const make_signed_double_word_t<word> a, const word q,
                 const make_signed_t<word> q_inv) {
  auto res = detail::__montgomery_reduction_lazy<word>(a, q, q_inv);
  if (res < 0) res += q;
  return static_cast<word>(res);
}

/**
 * @brief Calculates (a + b) % q
 *
 * @tparam word  either uint32_t or uint64_t
 * @param a      any number in range [0, q-1]
 * @param b      any number in range [0, q-1]
 * @param q      an odd prime smaller than 2^31
 * @return word  output in range [0, q-1]
 */
template <typename word>
__inline__ __host__ __device__ word Add(const word a, const word b,
                                        const word q) {
  word res = a + b;
  if (res >= q) res -= q;
  return res;
}

/**
 * @brief Calculates (a - b) % q
 *
 * @tparam word      either uint32_t or uint64_t
 * @param a          any number in range [0, q-1]
 * @param b          any number in range [0, q-1]
 * @param q          an odd prime smaller than 2^31
 * @return uint32_t  output in range [0, q-1]
 */
template <typename word>
__inline__ __host__ __device__ word Sub(const word a, const word b,
                                        const word q) {
  using signed_word = make_signed_t<word>;
  signed_word res = static_cast<signed_word>(a) - static_cast<signed_word>(b);
  if (res < 0) res += q;
  return static_cast<word>(res);
}

/**
 * @brief Calculates (b - a) % q
 *
 * @tparam word      either uint32_t or uint64_t
 * @param a          any number in range [0, q-1]
 * @param b          any number in range [0, q-1]
 * @param q          an odd prime smaller than 2^31
 * @return uint32_t  output in range [0, q-1]
 */
template <typename word>
__inline__ __host__ __device__ word SubOpposite(const word a, const word b,
                                                const word q) {
  return Sub<word>(b, a, q);
}

/**
 * @brief Calculates (-a) % q
 *
 * @tparam word  either uint32_t or uint64_t
 * @param a      any number in range [0, q-1]
 * @param q      an odd prime smaller than 2^31
 * @return word  output in range [0, q-1]
 */
template <typename word>
__inline__ __host__ __device__ word Negate(const word a, const word q) {
  word res = 0;
  if (a > 0) res = q - a;
  return res;
}

/**
 * @brief Calculates a % q in range [-(q-1)/2, (q-1)/2]
 *
 * @tparam word                 either uint32_t or uint64_t
 * @param a                     any number in range [0, q-1]
 * @param q                     an odd prime smaller than 2^31
 * @return make_signed_t<word>  signed output in range [-(q-1)/2, (q-1)/2]
 */
template <typename word>
__inline__ __host__ __device__ make_signed_t<word> Normalize(const word a,
                                                             const word q) {
  using signed_word = make_signed_t<word>;
  signed_word res = static_cast<signed_word>(a);
  if (a > (q >> 1)) res -= static_cast<signed_word>(q);
  return res;
}

/**
 * @brief Performs lazy Montgomery modular reduction (a * b) % q and returns
 * result in [0, q)
 *
 * @tparam word  either uint32_t or uint64_t
 * @param a      any number in range [0, q-1]
 * @param b      any number in range [0, q-1]
 * @param q      an odd prime smaller than 2^31
 * @return word  output in range [0, q-1]
 */
template <typename word>
__inline__ __device__ word MultMontgomery(const word a, const word b,
                                          const word q,
                                          const make_signed_t<word> q_inv) {
  using signed_word = make_signed_t<word>;
  signed_word res = detail::__mult_montgomery_lazy<word>(
      static_cast<signed_word>(a), static_cast<signed_word>(b), q, q_inv);
  if (res < 0) res += q;
  return static_cast<word>(res);
}

/**
 * @brief Perform bit reverse of index i
 *
 * @param i              index
 * @param bits           number of bits, should be in range [1, 32]
 * @return unsigned int  bit reversed index
 */
__device__ __inline__ unsigned int BitReverse(const unsigned int i,
                                              const unsigned int bits) {
  return (__brev(i) >> (32 - bits));
}

template <typename word>
__device__ __inline__ word StreamingLoad(const word *src) {
  static_assert("Must use correct template specialization");
  return 0;
}

template <>
__device__ __inline__ uint64_t StreamingLoad(const uint64_t *src) {
  uint64_t dst;
  asm("ld.global.cs.u64 %0, [%1];" : "=l"(dst) : "l"(src));
  return dst;
}

template <>
__device__ __inline__ uint32_t StreamingLoad(const uint32_t *src) {
  uint32_t dst;
  asm("ld.global.cs.u32 %0, [%1];" : "=r"(dst) : "l"(src));
  return dst;
}

template <>
__device__ __inline__ int64_t StreamingLoad(const int64_t *src) {
  int64_t dst;
  asm("ld.global.cs.s64 %0, [%1];" : "=l"(dst) : "l"(src));
  return dst;
}

template <>
__device__ __inline__ int32_t StreamingLoad(const int32_t *src) {
  int32_t dst;
  asm("ld.global.cs.s32 %0, [%1];" : "=r"(dst) : "l"(src));
  return dst;
}

template <typename word>
__device__ __inline__ word StreamingLoadConst(const word *src) {
  static_assert("Must use correct template specialization");
  return 0;
}

template <>
__device__ __inline__ uint64_t StreamingLoadConst(const uint64_t *src) {
  uint64_t dst;
  asm("ldu.global.u64 %0, [%1];" : "=l"(dst) : "l"(src));
  return dst;
}

template <>
__device__ __inline__ uint32_t StreamingLoadConst(const uint32_t *src) {
  uint32_t dst;
  asm("ldu.global.u32 %0, [%1];" : "=r"(dst) : "l"(src));
  return dst;
}

template <>
__device__ __inline__ int64_t StreamingLoadConst(const int64_t *src) {
  int64_t dst;
  asm("ldu.global.s64 %0, [%1];" : "=l"(dst) : "l"(src));
  return dst;
}

template <>
__device__ __inline__ int32_t StreamingLoadConst(const int32_t *src) {
  int32_t dst;
  asm("ldu.global.s32 %0, [%1];" : "=r"(dst) : "l"(src));
  return dst;
}

template <typename word, int size>
__device__ __inline__ void VectorizedMove(word *dst, const word *src) {
  constexpr int Byte = sizeof(word) * size;
  constexpr int LoopNum = Byte / 16;
  if constexpr (Byte >= 16) {
    const int4 *src4 = reinterpret_cast<const int4 *>(src);
    int4 *dst4 = reinterpret_cast<int4 *>(dst);
#pragma unroll
    for (int i = 0; i < LoopNum; i++) {
      dst4[i] = src4[i];
    }
  } else if constexpr (Byte == 8) {
    const int2 *src2 = reinterpret_cast<const int2 *>(src);
    int2 *dst2 = reinterpret_cast<int2 *>(dst);
    dst2[0] = src2[0];
  } else if constexpr (Byte == 4) {
    dst[0] = src[0];
  }
}

// Reconstructs the centred signed magnitude from 1/2/3 limbs. Inputs must be
// in the centred range for those limbs; 64-bit words use at most 2 limbs.
template <typename word>
__inline__ __device__ void ReconstructCenteredCRT(
    bool &is_negative,
    uint128_t &abs_mag,
    word z_0,
    word z_1,
    word z_2,
    const word *primes,
    word inv_q0_mod_q1,
    word inv_q0q1_mod_q2,
    int num_available_limbs) {
  using dword = make_unsigned_double_word_t<word>;
  if (num_available_limbs < 2) {
    word q_0 = primes[0];
    word half_q_0 = q_0 >> 1;
    if (z_0 <= half_q_0) {
      is_negative = false;
      abs_mag = z_0;
    } else {
      is_negative = true;
      abs_mag = q_0 - z_0;
    }
  } else if (sizeof(word) == 8 || num_available_limbs == 2) {
    dword q_0 = primes[0];
    dword q_1 = primes[1];
    dword z_0_w = z_0;
    dword z_1_w = z_1;
    dword z0_mod_q1 = z_0_w % q_1;
    dword diff = (z_1_w >= z0_mod_q1) ? (z_1_w - z0_mod_q1)
                                      : (z_1_w + q_1 - z0_mod_q1);
    dword v = (diff * static_cast<dword>(inv_q0_mod_q1)) % q_1;
    dword z_val = z_0_w + q_0 * v;
    dword M = q_0 * q_1;
    dword half_M = M >> 1;
    if (z_val <= half_M) {
      is_negative = false;
      abs_mag = z_val;
    } else {
      is_negative = true;
      abs_mag = M - z_val;
    }
  } else {
    uint128_t q_0 = primes[0];
    uint128_t q_1 = primes[1];
    uint128_t q_2 = primes[2];
    uint128_t z0_mod_q1 = static_cast<uint128_t>(z_0) % q_1;
    uint128_t diff1 = (static_cast<uint128_t>(z_1) >= z0_mod_q1)
                          ? (static_cast<uint128_t>(z_1) - z0_mod_q1)
                          : (static_cast<uint128_t>(z_1) + q_1 - z0_mod_q1);
    uint128_t v1 = (diff1 * static_cast<uint128_t>(inv_q0_mod_q1)) % q_1;
    uint128_t z_01 = static_cast<uint128_t>(z_0) + q_0 * v1;
    uint128_t q_01 = q_0 * q_1;

    uint128_t z01_mod_q2 = z_01 % q_2;
    uint128_t diff2 = (static_cast<uint128_t>(z_2) >= z01_mod_q2)
                          ? (static_cast<uint128_t>(z_2) - z01_mod_q2)
                          : (static_cast<uint128_t>(z_2) + q_2 - z01_mod_q2);
    uint128_t v2 = (diff2 * static_cast<uint128_t>(inv_q0q1_mod_q2)) % q_2;
    uint128_t z_val = z_01 + q_01 * v2;
    uint128_t M = q_01 * q_2;
    uint128_t half_M = M >> 1;
    if (z_val <= half_M) {
      is_negative = false;
      abs_mag = z_val;
    } else {
      is_negative = true;
      abs_mag = M - z_val;
    }
  }
}

}  // namespace basic
}  // namespace cheddar