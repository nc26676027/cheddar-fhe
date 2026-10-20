#include "common/Assert.h"
#include "common/Basic.cuh"
#include "common/CommonUtils.h"
#include "common/ConstantMemory.cuh"
#include "common/DoubleWord.h"
#include "common/PrimeUtils.h"
#include "common/PtrList.h"
#include "core/ElementWise.h"

namespace {
// https://artificial-mind.net/blog/2020/10/31/constexpr-for
template <int Start, int End, int Inc = 1, class Func>
constexpr void constexpr_for(Func &&func) {
  if constexpr (Start < End) {
    func(std::integral_constant<decltype(Start), Start>());
    constexpr_for<Start + Inc, End, Inc>(std::forward<Func>(func));
  }
}
}  // namespace

namespace cheddar {
namespace kernel {

// ----- ElementWise functions ----- //

// The order of arguments is:
// Result PointerList
// Some metadata
// Input CPointerList

// dst = src_1 + src_2 + ... + src_last;
template <typename word, int num_poly, typename... PtrLists>
__global__ void Sum(OutputPtrList<word, num_poly> dst, const word *primes,
                    int num_q_primes, const PtrLists... srcs) {
  static_assert(sizeof...(PtrLists) > 0, "Sum must have at least one source");
  static_assert((std::is_same_v<PtrLists, InputPtrList<word, num_poly>> && ...),
                "Sum must have InputPtrList as the last source");

  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);

  word result[num_poly] = {0};
  bool aux_part = (prime_index >= num_q_primes);

  // We can use this lambda fold expression to expand the variadic template for
  // any function
  // int k = 0;
  (
      [&] {
        int src_index = i;
        if (aux_part) {
          src_index += srcs.extra_;
        }
#pragma unroll
        for (int j = 0; j < num_poly; j++) {
          const word src_value =
              basic::StreamingLoad(srcs.ptrs_[j] + src_index);
          result[j] = basic::Add(result[j], src_value, prime);
        }
        // ++k;
      }(),
      ...);

  // Store the result
#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    dst.ptrs_[j][i] = result[j];
  }
}

// dst = src_1 - src_2;
template <typename word, int num_poly>
__global__ void Sub(OutputPtrList<word, num_poly> dst, const word *primes,
                    int num_q_primes, const InputPtrList<word, num_poly> src1,
                    const InputPtrList<word, num_poly> src2) {
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  int src1_index = i;
  int src2_index = i;
  if (prime_index >= num_q_primes) {
    src1_index += src1.extra_;
    src2_index += src2.extra_;
  }

#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    const word src1_value = basic::StreamingLoad(src1.ptrs_[j] + src1_index);
    const word src2_value = basic::StreamingLoad(src2.ptrs_[j] + src2_index);
    dst.ptrs_[j][i] = basic::Sub(src1_value, src2_value, prime);
  }
}

// dst = -src;
template <typename word, int num_poly>
__global__ void Neg(OutputPtrList<word, num_poly> dst, const word *primes,
                    int num_q_primes, const InputPtrList<word, num_poly> src) {
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  int src_index = i;
  if (prime_index >= num_q_primes) {
    src_index += src.extra_;
  }

#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    const word src_value = basic::StreamingLoad(src.ptrs_[j] + src_index);
    dst.ptrs_[j][i] = basic::Negate(src_value, prime);
  }
}

// dst = src_1 * src_2;
template <typename word, int num_poly>
__global__ void Mult(OutputPtrList<word, num_poly> dst, const word *primes,
                     const make_signed_t<word> *inv_primes, int num_q_primes,
                     const InputPtrList<word, num_poly> src1,
                     const InputPtrList<word, num_poly> src2) {
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const make_signed_t<word> inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);
  int src1_index = i;
  int src2_index = i;
  if (prime_index >= num_q_primes) {
    src1_index += src1.extra_;
    src2_index += src2.extra_;
  }

#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    const word src1_value = basic::StreamingLoad(src1.ptrs_[j] + src1_index);
    const word src2_value = basic::StreamingLoad(src2.ptrs_[j] + src2_index);
    dst.ptrs_[j][i] =
        basic::MultMontgomery(src1_value, src2_value, prime, inv_prime);
  }
}

// dst = src - const_src;
template <typename word, int num_poly>
__global__ void AddConst(OutputPtrList<word, num_poly> dst, const word *primes,
                         int num_q_primes,
                         const InputPtrList<word, num_poly> src,
                         const InputPtrList<word, 1> const_src) {
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  int src_index = i;
  int const_src_index = prime_index;
  if (prime_index >= num_q_primes) {
    src_index += src.extra_;
    const_src_index += const_src.extra_;
  }
  const word const_src_value =
      basic::StreamingLoadConst(const_src.ptrs_[0] + const_src_index);

#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    const word src_value = basic::StreamingLoad(src.ptrs_[j] + src_index);
    dst.ptrs_[j][i] = basic::Add(src_value, const_src_value, prime);
  }
}

// dst = src - const_src;
template <typename word, int num_poly>
__global__ void SubConst(OutputPtrList<word, num_poly> dst, const word *primes,
                         int num_q_primes,
                         const InputPtrList<word, num_poly> src,
                         const InputPtrList<word, 1> const_src) {
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  int src_index = i;
  int const_src_index = prime_index;
  if (prime_index >= num_q_primes) {
    src_index += src.extra_;
    const_src_index += const_src.extra_;
  }
  const word const_src_value =
      basic::StreamingLoadConst(const_src.ptrs_[0] + const_src_index);

#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    const word src_value = basic::StreamingLoad(src.ptrs_[j] + src_index);
    dst.ptrs_[j][i] = basic::Sub(src_value, const_src_value, prime);
  }
}

// dst = const_src - src;
template <typename word, int num_poly>
__global__ void SubOppositeConst(OutputPtrList<word, num_poly> dst,
                                 const word *primes, int num_q_primes,
                                 const InputPtrList<word, num_poly> src,
                                 const InputPtrList<word, 1> const_src) {
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  int src_index = i;
  int const_src_index = prime_index;
  if (prime_index >= num_q_primes) {
    src_index += src.extra_;
    const_src_index += const_src.extra_;
  }
  const word const_src_value =
      basic::StreamingLoadConst(const_src.ptrs_[0] + const_src_index);

#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    const word src_value = basic::StreamingLoad(src.ptrs_[j] + src_index);
    dst.ptrs_[j][i] = basic::Sub(const_src_value, src_value, prime);
  }
}

// CAccum/PAccum version without src0
// PtrLists: CPAcuumInputPtrList
template <typename word, int num_poly, bool const_accum, typename... PtrLists>
__global__ void CPAccum(OutputPtrList<word, num_poly> dst, const word *primes,
                        const make_signed_t<word> *inv_primes, int num_q_primes,
                        const PtrLists... srcs) {
  static_assert(sizeof...(PtrLists) > 0,
                "CPAccum must have at least one source");
  static_assert(
      (std::is_same_v<PtrLists, CPAccumInputPtrList<word, num_poly>> && ...),
      "CPAccum must have CPAccumInputPtrList as the last source");

  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);

  word result[num_poly] = {0};
  bool aux_part = (prime_index >= num_q_primes);

  // We can use this lambda fold expression to expand the variadic template
  // for any function
  (
      [&] {
        int src_index = i;
        int common_index;
        // CAccum vs. PAccum
        if constexpr (const_accum) {
          common_index = prime_index;
        } else {
          common_index = i;
        }
        if (aux_part) {
          src_index += srcs.extra_;
          common_index += srcs.common_extra_;
        }
        word common_temp = 0;
        // CAccum vs. PAccum
        if constexpr (const_accum) {
          common_temp =
              basic::StreamingLoadConst(srcs.common_ptr_ + common_index);
        } else {
          common_temp = basic::StreamingLoad(srcs.common_ptr_ + common_index);
        }

#pragma unroll
        for (int j = 0; j < num_poly; j++) {
          const word src_value =
              basic::StreamingLoad(srcs.ptrs_[j] + src_index);
          word mult =
              basic::MultMontgomery(common_temp, src_value, prime, inv_prime);
          result[j] = basic::Add(result[j], mult, prime);
        }
      }(),
      ...);

// Store the result
#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    dst.ptrs_[j][i] = result[j];
  }
}

// CAccum/PAccum version with src0
// PtrLists: CPAccumInputPtrList
template <typename word, int num_poly, bool const_accum, typename... PtrLists>
__global__ void CPAccumAdd(OutputPtrList<word, num_poly> dst,
                           const word *primes,
                           const make_signed_t<word> *inv_primes,
                           int num_q_primes,
                           const InputPtrList<word, num_poly> src0,
                           const PtrLists... srcs) {
  static_assert(sizeof...(PtrLists) > 0,
                "CPAccumAdd must have at least one source");
  static_assert(
      (std::is_same_v<PtrLists, CPAccumInputPtrList<word, num_poly>> && ...),
      "CPAccumAdd must have CPAccumInputPtrList as the last source");

  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);

  word result[num_poly] = {0};
  bool aux_part = (prime_index >= num_q_primes);

  // Only this part is different from the upper version
  int src0_index = i;
  if (aux_part) {
    src0_index += src0.extra_;
  }
#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    result[j] = basic::StreamingLoad(src0.ptrs_[j] + src0_index);
  }

  // We can use this lambda fold expression to expand the variadic template
  // for any function
  (
      [&] {
        int src_index = i;
        int common_index;
        // CAccum vs. PAccum
        if constexpr (const_accum) {
          common_index = prime_index;
        } else {
          common_index = i;
        }
        if (aux_part) {
          src_index += srcs.extra_;
          common_index += srcs.common_extra_;
        }
        word common_temp = 0;
        // CAccum vs. PAccum
        if constexpr (const_accum) {
          common_temp =
              basic::StreamingLoadConst(srcs.common_ptr_ + common_index);
        } else {
          common_temp = basic::StreamingLoad(srcs.common_ptr_ + common_index);
        }

#pragma unroll
        for (int j = 0; j < num_poly; j++) {
          const word src_value =
              basic::StreamingLoad(srcs.ptrs_[j] + src_index);
          word mult =
              basic::MultMontgomery(common_temp, src_value, prime, inv_prime);
          result[j] = basic::Add(result[j], mult, prime);
        }
      }(),
      ...);

// Store the result
#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    dst.ptrs_[j][i] = result[j];
  }
}

// dst = permute(src1, r1) + permute(src2, r2) + ... + permute(src_last,
// r_last); {src1, r1} embedded in a single PtrList
template <typename word, int num_poly, typename... PtrLists>
__global__ void Permute(OutputPtrList<word, num_poly> dst, int num_q_primes,
                        const PermuteInputPtrList<word, num_poly> src1) {
  int log_degree = cm_log_degree();
  int degree = 1 << log_degree;
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  uint32_t x_idx = i & (degree - 1);
  uint32_t x_idx_rev = basic::BitReverse(x_idx, log_degree + 1) + 1;

  uint32_t gf = src1.galois_factor_;
  int src1_idx = (prime_index << log_degree) +
                 basic::BitReverse(x_idx_rev * gf - 1, log_degree + 1);
  if (prime_index >= num_q_primes) {
    src1_idx += src1.extra_;
  }

  for (int j = 0; j < num_poly; j++) {
    const word src1_value = basic::StreamingLoad(src1.ptrs_[j] + src1_idx);
    dst.ptrs_[j][i] = src1_value;
  }
}

// dst = permute(src1, r1) + permute(src2, r2) + ... + permute(src_last,
// r_last); {src1, r1} embedded in a single PtrList
template <typename word, int num_poly, typename... PtrLists>
__global__ void PermuteAccum(OutputPtrList<word, num_poly> dst,
                             const word *primes, int num_q_primes,
                             const PtrLists... srcs) {
  static_assert(sizeof...(PtrLists) > 0,
                "PermuteAccum must have at least two srcs");
  static_assert(
      (std::is_same_v<PtrLists, PermuteInputPtrList<word, num_poly>> && ...),
      "PermuteAccum must have PermuteInputPtrList as the last src");

  int log_degree = cm_log_degree();
  int degree = 1 << log_degree;
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  uint32_t x_idx = i & (degree - 1);
  uint32_t x_idx_rev = basic::BitReverse(x_idx, log_degree + 1) + 1;
  const word prime = basic::StreamingLoadConst(primes + prime_index);

  word result[num_poly] = {0};
  bool aux_part = (prime_index >= num_q_primes);
  int y_offset = prime_index << log_degree;

  // We can use this lambda fold expression to expand the variadic template
  // for any function
  (
      [&] {
        uint32_t gf = srcs.galois_factor_;
        int src_idx = basic::BitReverse(x_idx_rev * gf - 1, log_degree + 1);
        if (aux_part) {
          src_idx += srcs.extra_;
        }
#pragma unroll
        for (int j = 0; j < num_poly; j++) {
          // This is somehow coalesced
          const word src_value =
              basic::StreamingLoad(srcs.ptrs_[j] + src_idx + y_offset);
          result[j] = basic::Add(result[j], src_value, prime);
        }
      }(),
      ...);

// Store the result
#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    dst.ptrs_[j][i] = result[j];
  }
}

// dst = src0 + permute(src1, r1) + permute(src2, r2) + ... + permute(src_last,
// r_last); {src1, r1} embedded in a single PtrList
template <typename word, int num_poly, typename... PtrLists>
__global__ void PermuteAccumAdd(OutputPtrList<word, num_poly> dst,
                                const word *primes, int num_q_primes,
                                const InputPtrList<word, num_poly> src0,
                                const PtrLists... srcs) {
  static_assert(sizeof...(PtrLists) > 0,
                "PermuteAccum must have at least two srcs");
  static_assert(
      (std::is_same_v<PtrLists, PermuteInputPtrList<word, num_poly>> && ...),
      "PermuteAccum must have PermuteInputPtrList as the last src");

  int log_degree = cm_log_degree();
  int degree = 1 << log_degree;
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  uint32_t x_idx = i & (degree - 1);
  uint32_t x_idx_rev = basic::BitReverse(x_idx, log_degree + 1) + 1;
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  bool aux_part = (prime_index >= num_q_primes);

  word result[num_poly];
  int src0_index = i;
  if (aux_part) {
    src0_index += src0.extra_;
  }
#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    result[j] = basic::StreamingLoad(src0.ptrs_[j] + src0_index);
  }

  int y_offset = prime_index << log_degree;

  // We can use this lambda fold expression to expand the variadic template
  // for any function
  (
      [&] {
        uint32_t gf = srcs.galois_factor_;
        int src_idx = basic::BitReverse(x_idx_rev * gf - 1, log_degree + 1);
        if (aux_part) {
          src_idx += srcs.extra_;
        }
#pragma unroll
        for (int j = 0; j < num_poly; j++) {
          // This is somehow coalesced
          const word src_value =
              basic::StreamingLoad(srcs.ptrs_[j] + src_idx + y_offset);
          result[j] = basic::Add(result[j], src_value, prime);
        }
      }(),
      ...);

// Store the result
#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    dst.ptrs_[j][i] = result[j];
  }
}

// (dst_bx, dst_ax, dst_rx) = (b1 * b2, b1 * a2 + a1 * b2, a1 * a2);
template <typename word /*, int num_poly = 2*/>
__global__ void Tensor(OutputPtrList<word, 3> dst, const word *primes,
                       const make_signed_t<word> *inv_primes, int num_q_primes,
                       const InputPtrList<word, 2> src1,
                       const InputPtrList<word, 2> src2) {
  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);
  int src1_index = i;
  int src2_index = i;
  if (prime_index >= num_q_primes) {
    src1_index += src1.extra_;
    src2_index += src2.extra_;
  }

  // Load everything first
  signed_word b1 = basic::StreamingLoad(src1.ptrs_[0] + src1_index);
  signed_word a1 = basic::StreamingLoad(src1.ptrs_[1] + src1_index);
  signed_word b2 = basic::StreamingLoad(src2.ptrs_[0] + src2_index);
  signed_word a2 = basic::StreamingLoad(src2.ptrs_[1] + src2_index);

  // karatsuba multiplication
  signed_word b1_plus_a1 = (b1 - prime) + a1;  // [-q, q - 2] range
  signed_word b2_plus_a2 = (b2 - prime) + a2;  // [-q, q - 2] range
  // [-q(q-2), q^2] range for the multiplication
  auto a_mult = basic::detail::__mult_wide<signed_word>(b1_plus_a1, b2_plus_a2);
  word new_ax = basic::ReduceMontgomery(a_mult, prime, inv_prime);

  word b1_times_b2 = basic::MultMontgomery<word>(b1, b2, prime, inv_prime);
  word a1_times_a2 = basic::MultMontgomery<word>(a1, a2, prime, inv_prime);
  // [-q^2 + 2, q^2] range
  new_ax = basic::Sub(new_ax, b1_times_b2, prime);
  new_ax = basic::Sub(new_ax, a1_times_a2, prime);

  // bx, ax, rx
  dst.ptrs_[0][i] = b1_times_b2;
  dst.ptrs_[1][i] = new_ax;
  dst.ptrs_[2][i] = a1_times_a2;
}

// dst = src_1 - src_2;
template <typename word /*, int num_poly = 2*/>
__global__ void TensorSquare(OutputPtrList<word, 3> dst, const word *primes,
                             const make_signed_t<word> *inv_primes,
                             int num_q_primes,
                             const InputPtrList<word, 2> src1) {
  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);
  int src1_index = i;
  if (prime_index >= num_q_primes) {
    src1_index += src1.extra_;
  }

  // Load everything first
  word b1 = basic::StreamingLoad(src1.ptrs_[0] + src1_index);
  word a1 = basic::StreamingLoad(src1.ptrs_[1] + src1_index);

  // bx, ax, rx
  dst.ptrs_[0][i] = basic::MultMontgomery<word>(b1, b1, prime, inv_prime);
  word b1_times_a1 = basic::MultMontgomery<word>(b1, a1, prime, inv_prime);
  dst.ptrs_[1][i] = basic::Add<word>(b1_times_a1, b1_times_a1, prime);
  dst.ptrs_[2][i] = basic::MultMontgomery<word>(a1, a1, prime, inv_prime);
}

// Special kernels for bootstrapping

template <typename word>
__global__ void ModUpToMax1(word *dst, const word *primes, const word q0,
                            const word *src) {
  int log_degree = cm_log_degree();
  int degree = 1 << log_degree;
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  int degree_idx = i & (degree - 1);
  const word prime = basic::StreamingLoadConst(primes + prime_index);

  word value = basic::StreamingLoad(src + degree_idx);
  bool negative = value > (q0 >> 1);
  word magnitude = negative ? q0 - value : value;
  word residue = magnitude % prime;
  // Match the centered lift in ModUpToMax2 and bound the EvalMod input.
  dst[i] = negative ? basic::Negate(residue, prime) : residue;
}

template <typename word>
__global__ void ModUpToMax2(word *dst, const word *primes, const word q0,
                            const word q1, const word *src) {
  using signed_word = make_signed_t<word>;
  using signed_d_word = make_signed_double_word_t<word>;

  int log_degree = cm_log_degree();
  int degree = 1 << log_degree;
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  int degree_idx = i & (degree - 1);
  const word prime = basic::StreamingLoadConst(primes + prime_index);

  signed_d_word q_prod = basic::detail::__mult_wide(q0, q1);
  signed_d_word half_q_prod = (q_prod >> 1);

  word src0 = basic::StreamingLoad(src + degree_idx);
  word src1 = basic::StreamingLoad(src + degree_idx + degree);
  signed_d_word res = basic::detail::__mult_wide(src0, q1) +
                      basic::detail::__mult_wide(src1, q0);

  // res is in range [0, 2 * q0q1 - 1]
  // convert it into (-q0q1 / 2, q0q1 / 2)
  if (res >= q_prod) res -= q_prod;
  if (res > half_q_prod) res -= q_prod;
  auto reduced =
      static_cast<signed_word>(res % static_cast<signed_d_word>(prime));
  if (reduced < 0) reduced += prime;
  dst[i] = reduced;
}

template <typename word, int num_poly>
__global__ void MultImaginaryUnit(OutputPtrList<word, num_poly> dst,
                                  const word *primes,
                                  const make_signed_t<word> *inv_primes,
                                  int num_q_primes,
                                  const InputPtrList<word, num_poly> src,
                                  const InputPtrList<word, 1> i_unit) {
  int log_degree = cm_log_degree();
  int half_degree = 1 << (log_degree - 1);
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const word inv_prime = basic::StreamingLoadConst(inv_primes + prime_index);

  int src_index = i;
  int i_unit_index = prime_index << log_degree;
  if (prime_index >= num_q_primes) {
    i_unit_index += i_unit.extra_;
    src_index += src.extra_;
  }

  word psi_n_over_2 =
      basic::StreamingLoadConst(i_unit.ptrs_[0] + (prime_index << log_degree));
  if (i & half_degree) {  // if (degree_idx >= half_degree)
    psi_n_over_2 = prime - psi_n_over_2;
  }

#pragma unroll
  for (int j = 0; j < num_poly; j++) {
    word src_value = basic::StreamingLoad(src.ptrs_[j] + src_index);
    dst.ptrs_[j][i] =
        basic::MultMontgomery(src_value, psi_n_over_2, prime, inv_prime);
  }
}

template <typename word, int MaxBatch = 6, int MaxChunks = 6>
struct KeySwitchPAccumBatchParams {
  const word *key_bx[MaxChunks];
  const word *key_ax[MaxChunks];
  int key_extra[MaxChunks];
  const word *modup[MaxBatch][MaxChunks];
  int modup_extra[MaxBatch][MaxChunks];
  const word *orig_bx[MaxBatch];
  const word *orig_ax[MaxBatch];
  word *dst_bx[MaxBatch];
  word *dst_ax[MaxBatch];
  const word *p_prod;
  int num_chunks;
};

template <typename word, int B>
__global__ void KeySwitchPAccumBatch(
    KeySwitchPAccumBatchParams<word, 6, 6> params, const word *primes,
    const make_signed_t<word> *inv_primes, int num_q_primes) {
  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);
  bool aux_part = (prime_index >= num_q_primes);

  word res_bx[B] = {0};
  word res_ax[B] = {0};

  for (int m = 0; m < params.num_chunks; ++m) {
    int key_idx = i + (aux_part ? params.key_extra[m] : 0);
    const word k_bx = basic::StreamingLoad(params.key_bx[m] + key_idx);
    const word k_ax = basic::StreamingLoad(params.key_ax[m] + key_idx);
#pragma unroll
    for (int b = 0; b < B; ++b) {
      int modup_idx = i + (aux_part ? params.modup_extra[b][m] : 0);
      const word mu = basic::StreamingLoad(params.modup[b][m] + modup_idx);
      res_bx[b] = basic::Add(
          res_bx[b], basic::MultMontgomery(mu, k_bx, prime, inv_prime), prime);
      res_ax[b] = basic::Add(
          res_ax[b], basic::MultMontgomery(mu, k_ax, prime, inv_prime), prime);
    }
  }

  if (!aux_part && params.p_prod != nullptr) {
    const word p_val = basic::StreamingLoadConst(params.p_prod + prime_index);
#pragma unroll
    for (int b = 0; b < B; ++b) {
      if (params.orig_bx[b] != nullptr) {
        const word obx = basic::StreamingLoad(params.orig_bx[b] + i);
        res_bx[b] = basic::Add(
            res_bx[b], basic::MultMontgomery(p_val, obx, prime, inv_prime),
            prime);
      }
      if (params.orig_ax[b] != nullptr) {
        const word oax = basic::StreamingLoad(params.orig_ax[b] + i);
        res_ax[b] = basic::Add(
            res_ax[b], basic::MultMontgomery(p_val, oax, prime, inv_prime),
            prime);
      }
    }
  }

#pragma unroll
  for (int b = 0; b < B; ++b) {
    params.dst_bx[b][i] = res_bx[b];
    params.dst_ax[b][i] = res_ax[b];
  }
}

template <typename word, int MaxOut = 6, int MaxBasis = 8>
struct MultiCAccumParams {
  const word *ct_bx[MaxBasis];
  const word *ct_ax[MaxBasis];
  int ct_extra[MaxBasis];
  const word *const_ptrs[MaxOut][MaxBasis];
  int const_extra[MaxOut][MaxBasis];
  const word *c0_ptrs[MaxOut];
  int c0_extra[MaxOut];
  const word *init_bx[MaxOut];
  const word *init_ax[MaxOut];
  int init_extra[MaxOut];
  word *dst_bx[MaxOut];
  word *dst_ax[MaxOut];
  int num_basis;
  bool has_init;
};

template <typename word, int M>
__global__ void MultiCAccum(MultiCAccumParams<word, 6, 8> params,
                            const word *primes,
                            const make_signed_t<word> *inv_primes,
                            int num_q_primes) {
  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);
  bool aux_part = (prime_index >= num_q_primes);

  word res_bx[M] = {0};
  word res_ax[M] = {0};
  if (params.has_init) {
#pragma unroll
    for (int m = 0; m < M; ++m) {
      int init_idx = i + (aux_part ? params.init_extra[m] : 0);
      res_bx[m] = basic::StreamingLoad(params.init_bx[m] + init_idx);
      res_ax[m] = basic::StreamingLoad(params.init_ax[m] + init_idx);
    }
  }

#pragma unroll
  for (int m = 0; m < M; ++m) {
    if (params.c0_ptrs[m] != nullptr) {
      int c0_idx = prime_index + (aux_part ? params.c0_extra[m] : 0);
      const word c0_val = basic::StreamingLoadConst(params.c0_ptrs[m] + c0_idx);
      res_bx[m] = basic::Add(res_bx[m], c0_val, prime);
    }
  }

  for (int k = 0; k < params.num_basis; ++k) {
    int ct_idx = i + (aux_part ? params.ct_extra[k] : 0);
    const word x_bx = basic::StreamingLoad(params.ct_bx[k] + ct_idx);
    const word x_ax = basic::StreamingLoad(params.ct_ax[k] + ct_idx);
#pragma unroll
    for (int m = 0; m < M; ++m) {
      const word *c_ptr = params.const_ptrs[m][k];
      if (c_ptr != nullptr) {
        int c_idx = prime_index + (aux_part ? params.const_extra[m][k] : 0);
        const word c_val = basic::StreamingLoadConst(c_ptr + c_idx);
        res_bx[m] = basic::Add(
            res_bx[m], basic::MultMontgomery(c_val, x_bx, prime, inv_prime),
            prime);
        res_ax[m] = basic::Add(
            res_ax[m], basic::MultMontgomery(c_val, x_ax, prime, inv_prime),
            prime);
      }
    }
  }

#pragma unroll
  for (int m = 0; m < M; ++m) {
    params.dst_bx[m][i] = res_bx[m];
    params.dst_ax[m][i] = res_ax[m];
  }
}

template <typename word>
__global__ void TensorAXYPBZ(OutputPtrList<word, 3> dst, const word *primes,
                             const make_signed_t<word> *inv_primes,
                             int num_q_primes, const InputPtrList<word, 2> x,
                             const InputPtrList<word, 2> y,
                             const InputPtrList<word, 2> z, bool has_z,
                             const word *a_ptr, int a_extra, const word *b_ptr,
                             int b_extra, bool is_square_no_a) {
  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);
  bool aux_part = (prime_index >= num_q_primes);

  int x_idx = i + (aux_part ? x.extra_ : 0);
  word x_b = basic::StreamingLoad(x.ptrs_[0] + x_idx);
  word x_a = basic::StreamingLoad(x.ptrs_[1] + x_idx);

  word b1_times_b2, new_ax, a1_times_a2;
  if (is_square_no_a) {
    b1_times_b2 = basic::MultMontgomery<word>(x_b, x_b, prime, inv_prime);
    word b1_times_a1 = basic::MultMontgomery<word>(x_b, x_a, prime, inv_prime);
    new_ax = basic::Add<word>(b1_times_a1, b1_times_a1, prime);
    a1_times_a2 = basic::MultMontgomery<word>(x_a, x_a, prime, inv_prime);
  } else {
    signed_word b1 = x_b;
    signed_word a1 = x_a;
    if (a_ptr != nullptr) {
      int a_idx = prime_index + (aux_part ? a_extra : 0);
      const word a_val = basic::StreamingLoadConst(a_ptr + a_idx);
      b1 = basic::MultMontgomery<word>(b1, a_val, prime, inv_prime);
      a1 = basic::MultMontgomery<word>(a1, a_val, prime, inv_prime);
    }
    int y_idx = i + (aux_part ? y.extra_ : 0);
    signed_word b2 = basic::StreamingLoad(y.ptrs_[0] + y_idx);
    signed_word a2 = basic::StreamingLoad(y.ptrs_[1] + y_idx);

    signed_word b1_plus_a1 = (b1 - prime) + a1;
    signed_word b2_plus_a2 = (b2 - prime) + a2;
    auto a_mult =
        basic::detail::__mult_wide<signed_word>(b1_plus_a1, b2_plus_a2);
    new_ax = basic::ReduceMontgomery(a_mult, prime, inv_prime);
    b1_times_b2 = basic::MultMontgomery<word>(b1, b2, prime, inv_prime);
    a1_times_a2 = basic::MultMontgomery<word>(a1, a2, prime, inv_prime);
    new_ax = basic::Sub(new_ax, b1_times_b2, prime);
    new_ax = basic::Sub(new_ax, a1_times_a2, prime);
  }

  if (b_ptr != nullptr) {
    int b_idx = prime_index + (aux_part ? b_extra : 0);
    const word b_val = basic::StreamingLoadConst(b_ptr + b_idx);
    if (has_z) {
      int z_idx = i + (aux_part ? z.extra_ : 0);
      const word z_b = basic::StreamingLoad(z.ptrs_[0] + z_idx);
      const word z_a = basic::StreamingLoad(z.ptrs_[1] + z_idx);
      b1_times_b2 = basic::Add(
          b1_times_b2, basic::MultMontgomery<word>(b_val, z_b, prime, inv_prime),
          prime);
      new_ax = basic::Add(
          new_ax, basic::MultMontgomery<word>(b_val, z_a, prime, inv_prime),
          prime);
    } else {
      b1_times_b2 = basic::Add(b1_times_b2, b_val, prime);
    }
  }

  dst.ptrs_[0][i] = b1_times_b2;
  dst.ptrs_[1][i] = new_ax;
  dst.ptrs_[2][i] = a1_times_a2;
}

template <typename word>
__global__ void TensorLinearFactor(OutputPtrList<word, 3> dst,
                                   const word *primes,
                                   const make_signed_t<word> *inv_primes,
                                   int num_q_primes,
                                   const InputPtrList<word, 2> sq,
                                   const InputPtrList<word, 2> x,
                                   const word *slope_ptr, int slope_extra,
                                   const word *offset_ptr, int offset_extra) {
  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);
  bool aux_part = (prime_index >= num_q_primes);

  int sq_idx = i + (aux_part ? sq.extra_ : 0);
  int x_idx = i + (aux_part ? x.extra_ : 0);
  signed_word b1 = basic::StreamingLoad(sq.ptrs_[0] + sq_idx);
  signed_word a1 = basic::StreamingLoad(sq.ptrs_[1] + sq_idx);
  word x_b = basic::StreamingLoad(x.ptrs_[0] + x_idx);
  word x_a = basic::StreamingLoad(x.ptrs_[1] + x_idx);

  const word slope_val = basic::StreamingLoadConst(
      slope_ptr + prime_index + (aux_part ? slope_extra : 0));
  const word offset_val = basic::StreamingLoadConst(
      offset_ptr + prime_index + (aux_part ? offset_extra : 0));

  word twice_b = basic::MultMontgomery<word>(x_b, slope_val, prime, inv_prime);
  word twice_a = basic::MultMontgomery<word>(x_a, slope_val, prime, inv_prime);
  signed_word b2 = basic::Sub(offset_val, twice_b, prime);
  signed_word a2 = basic::Negate(twice_a, prime);

  signed_word b1_plus_a1 = (b1 - prime) + a1;
  signed_word b2_plus_a2 = (b2 - prime) + a2;
  auto a_mult = basic::detail::__mult_wide<signed_word>(b1_plus_a1, b2_plus_a2);
  word new_ax = basic::ReduceMontgomery(a_mult, prime, inv_prime);
  word b1_times_b2 = basic::MultMontgomery<word>(b1, b2, prime, inv_prime);
  word a1_times_a2 = basic::MultMontgomery<word>(a1, a2, prime, inv_prime);
  new_ax = basic::Sub(new_ax, b1_times_b2, prime);
  new_ax = basic::Sub(new_ax, a1_times_a2, prime);

  dst.ptrs_[0][i] = b1_times_b2;
  dst.ptrs_[1][i] = new_ax;
  dst.ptrs_[2][i] = a1_times_a2;
}

template <typename word, int MaxB = 6>
struct TensorSharedYParams {
  word *dst_ptrs[MaxB][3];
  const word *src1_ptrs[MaxB][2];
  int src1_extra[MaxB];
  const word *y_ptrs[2];
  int y_extra;
  int B;
};

template <typename word, int MaxB = 6>
__global__ void TensorSharedY(TensorSharedYParams<word, MaxB> params,
                              const word *primes,
                              const make_signed_t<word> *inv_primes,
                              int num_q_primes) {
  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);
  bool aux_part = (prime_index >= num_q_primes);

  int y_idx = i + (aux_part ? params.y_extra : 0);
  signed_word b2 = basic::StreamingLoad(params.y_ptrs[0] + y_idx);
  signed_word a2 = basic::StreamingLoad(params.y_ptrs[1] + y_idx);
  signed_word b2_plus_a2 = (b2 - prime) + a2;

  #pragma unroll
  for (int b = 0; b < MaxB; ++b) {
    if (b >= params.B) break;
    int src1_idx = i + (aux_part ? params.src1_extra[b] : 0);
    signed_word b1 = basic::StreamingLoad(params.src1_ptrs[b][0] + src1_idx);
    signed_word a1 = basic::StreamingLoad(params.src1_ptrs[b][1] + src1_idx);

    signed_word b1_plus_a1 = (b1 - prime) + a1;
    auto a_mult = basic::detail::__mult_wide<signed_word>(b1_plus_a1, b2_plus_a2);
    word new_ax = basic::ReduceMontgomery(a_mult, prime, inv_prime);

    word b1_times_b2 = basic::MultMontgomery<word>(b1, b2, prime, inv_prime);
    word a1_times_a2 = basic::MultMontgomery<word>(a1, a2, prime, inv_prime);

    new_ax = basic::Sub(new_ax, b1_times_b2, prime);
    new_ax = basic::Sub(new_ax, a1_times_a2, prime);

    params.dst_ptrs[b][0][i] = b1_times_b2;
    params.dst_ptrs[b][1][i] = new_ax;
    params.dst_ptrs[b][2][i] = a1_times_a2;
  }
}

}  // namespace kernel

template <typename word>
ElementWiseHandler<word>::ElementWiseHandler(const Parameter<word> &param)
    : param_{param} {
  AssertTrue(param_.degree_ % kernel_block_dim_ == 0,
             "Invalid kernel block dim");
  if (!cm_populated_) {
    PopulateConstantMemory(param_);
    cm_populated_ = true;
  }
}

template <typename word>
void ElementWiseHandler<word>::AssertNPMatch(std::vector<DvView<word>> &dst,
                                             const NPInfo &np) const {
  int num_q_primes = np.num_main_ + np.num_ter_;
  int num_total_primes = np.GetNumTotal();
  for (const auto &dv_view : dst) {
    AssertTrue(dv_view.QSize() == num_q_primes * param_.degree_,
               "QSize mismatch");
    AssertTrue(dv_view.TotalSize() == num_total_primes * param_.degree_,
               "TotalSize mismatch");
  }
}

template <typename word>
void ElementWiseHandler<word>::Add(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &src1,
    const std::vector<DvConstView<word>> &src2) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly == static_cast<int>(src1.size()) &&
                 num_poly == static_cast<int>(src2.size()),
             "Add: Incompatible dst/src size");
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "Add: Invalid number of polynomials");
  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;
    OutputPtrList<word, j> dst_ptr_list(dst);
    InputPtrList<word, j> src1_ptr_list(src1);
    src1_ptr_list.extra_ = src1.at(0).QSize() - q_size;
    InputPtrList<word, j> src2_ptr_list(src2);
    src2_ptr_list.extra_ = src2.at(0).QSize() - q_size;

    kernel::Sum<word, j><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, primes, num_q_primes, src1_ptr_list, src2_ptr_list);
  });
}

template <typename word>
void ElementWiseHandler<word>::Sub(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &src1,
    const std::vector<DvConstView<word>> &src2) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly == static_cast<int>(src1.size()) &&
                 num_poly == static_cast<int>(src2.size()),
             "Sub: Incompatible dst/src size");
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "Sub: Invalid number of polynomials");
  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;
    OutputPtrList<word, j> dst_ptr_list(dst);
    InputPtrList<word, j> src1_ptr_list(src1);
    src1_ptr_list.extra_ = src1.at(0).QSize() - q_size;
    InputPtrList<word, j> src2_ptr_list(src2);
    src2_ptr_list.extra_ = src2.at(0).QSize() - q_size;

    kernel::Sub<word, j><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, primes, num_q_primes, src1_ptr_list, src2_ptr_list);
  });
}

template <typename word>
void ElementWiseHandler<word>::Neg(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &src1) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly == static_cast<int>(src1.size()),
             "Neg: Incompatible dst/src size");
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "Neg: Invalid number of polynomials");
  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;
    OutputPtrList<word, j> dst_ptr_list(dst);
    InputPtrList<word, j> src1_ptr_list(src1);
    src1_ptr_list.extra_ = src1.at(0).QSize() - q_size;

    kernel::Neg<word, j><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, primes, num_q_primes, src1_ptr_list);
  });
}

template <typename word>
void ElementWiseHandler<word>::Mult(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &src1,
    const std::vector<DvConstView<word>> &src2) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly == static_cast<int>(src1.size()) &&
                 num_poly == static_cast<int>(src2.size()),
             "Mult: Incompatible dst/src size");
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "Mult: Invalid number of polynomials");
  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = param_.GetInvPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;
    OutputPtrList<word, j> dst_ptr_list(dst);
    InputPtrList<word, j> src1_ptr_list(src1);
    src1_ptr_list.extra_ = src1.at(0).QSize() - q_size;
    InputPtrList<word, j> src2_ptr_list(src2);
    src2_ptr_list.extra_ = src2.at(0).QSize() - q_size;

    kernel::Mult<word, j><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, primes, inv_primes, num_q_primes, src1_ptr_list,
        src2_ptr_list);
  });
}

template <typename word>
void ElementWiseHandler<word>::PMult(std::vector<DvView<word>> &dst,
                                     const NPInfo &np,
                                     const std::vector<DvConstView<word>> &src1,
                                     const DvConstView<word> &src2) const {
  CPAccumWorker<false>(dst, np, {src1}, {src2});
}

template <typename word>
void ElementWiseHandler<word>::AddConst(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &src1,
    const DvConstView<word> &src_const) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly == static_cast<int>(src1.size()),
             "AddConst: Incompatible dst/src size");
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "AddConst: Invalid number of polynomials");
  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;
    OutputPtrList<word, j> dst_ptr_list(dst);
    InputPtrList<word, j> src1_ptr_list(src1);
    src1_ptr_list.extra_ = src1.at(0).QSize() - q_size;
    InputPtrList<word, 1> src_const_ptr_list(src_const);
    src_const_ptr_list.extra_ = src_const.QSize() - num_q_primes;

    kernel::AddConst<word, j><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, primes, num_q_primes, src1_ptr_list, src_const_ptr_list);
  });
}

template <typename word>
void ElementWiseHandler<word>::SubConst(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &src1,
    const DvConstView<word> &src_const) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly == static_cast<int>(src1.size()),
             "SubConst: Incompatible dst/src size");
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "SubConst: Invalid number of polynomials");
  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;
    OutputPtrList<word, j> dst_ptr_list(dst);
    InputPtrList<word, j> src1_ptr_list(src1);
    src1_ptr_list.extra_ = src1.at(0).QSize() - q_size;
    InputPtrList<word, 1> src_const_ptr_list(src_const);
    src_const_ptr_list.extra_ = src_const.QSize() - num_q_primes;

    kernel::SubConst<word, j><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, primes, num_q_primes, src1_ptr_list, src_const_ptr_list);
  });
}

template <typename word>
void ElementWiseHandler<word>::SubOppositeConst(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &src1,
    const DvConstView<word> &src_const) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly == static_cast<int>(src1.size()),
             "SubOppositeConst: Incompatible dst/src size");
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "SubOppositeConst: Invalid number of polynomials");
  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;
    OutputPtrList<word, j> dst_ptr_list(dst);
    InputPtrList<word, j> src1_ptr_list(src1);
    src1_ptr_list.extra_ = src1.at(0).QSize() - q_size;
    InputPtrList<word, 1> src_const_ptr_list(src_const);
    src_const_ptr_list.extra_ = src_const.QSize() - num_q_primes;

    kernel::SubOppositeConst<word, j><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, primes, num_q_primes, src1_ptr_list, src_const_ptr_list);
  });
}

template <typename word>
void ElementWiseHandler<word>::MultConst(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &src1,
    const DvConstView<word> &src_const) const {
  CPAccumWorker<true>(dst, np, {src1}, {src_const});
}

template <typename word>
void ElementWiseHandler<word>::Tensor(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &src1,
    const std::vector<DvConstView<word>> &src2) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(dst.size() == 3 && src1.size() == 2 && src2.size() == 2,
             "Tensor: Invalid number of polynomials");
  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = param_.GetInvPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  OutputPtrList<word, 3> dst_ptr_list(dst);
  InputPtrList<word, 2> src1_ptr_list(src1);
  src1_ptr_list.extra_ = src1.at(0).QSize() - q_size;

  if (src1.at(0).data() == src2.at(0).data() &&
      src1.at(1).data() == src2.at(1).data()) {
    kernel::TensorSquare<word><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, primes, inv_primes, num_q_primes, src1_ptr_list);
  } else {
    InputPtrList<word, 2> src2_ptr_list(src2);
    src2_ptr_list.extra_ = src2.at(0).QSize() - q_size;
    kernel::Tensor<word><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, primes, inv_primes, num_q_primes, src1_ptr_list,
        src2_ptr_list);
  }
}

template <typename word>
uint32_t ElementWiseHandler<word>::PermuteAmountToGaloisFactor(
    int permute_amount) const {
  if (permute_amount == -1) {
    return 2 * param_.degree_ - 1;
  }
  AssertTrue(permute_amount >= 0 && permute_amount <= param_.degree_ / 2,
             "Permute: Invalid permute amount");
  return param_.GetGaloisFactor(permute_amount);
}

template <typename word>
void ElementWiseHandler<word>::Permute(
    std::vector<DvView<word>> &dst, const NPInfo &np, int permute_amount,
    const std::vector<DvConstView<word>> &src1) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly == static_cast<int>(src1.size()),
             "Permute: Incompatible dst/src size");
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "Permute: Invalid number of polynomials");
  for (int i = 0; i < num_poly; i++) {
    AssertTrue(dst.at(i).data() != src1.at(i).data(),
               "Permute does not support inplace operation");
  }
  AssertNPMatch(dst, np);

  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;
  int half_degree = param_.degree_ / 2;

  // dim3 grid_dim(param_.degree_ / kernel_block_dim_, np.GetNumTotal());
  auto galois_factor = PermuteAmountToGaloisFactor(permute_amount);
  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;
    OutputPtrList<word, j> dst_ptr_list(dst);
    PermuteInputPtrList<word, j> src1_ptr_list(src1);
    src1_ptr_list.extra_ = src1.at(0).QSize() - q_size;
    src1_ptr_list.galois_factor_ = galois_factor;

    kernel::Permute<word, j><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, num_q_primes, src1_ptr_list);
  });
}

template <typename word>
void ElementWiseHandler<word>::PermuteAccum(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<int> &permute_amounts,
    const std::vector<std::vector<DvConstView<word>>> &srcs) const {
  if (kOptimizeAutomorphism) {
    PermuteAccumWorker(dst, np, permute_amounts, srcs);
    return;
  }

  // Naive repetition of Permute and add
  int num_poly = dst.size();
  int num_accum = permute_amounts.size();

  bool has_extra_ct = (static_cast<int>(srcs.size()) == num_accum + 1);
  AssertTrue(num_accum == static_cast<int>(srcs.size()) || has_extra_ct,
             "PermuteAccum: Incompatible srcs/permute_amounts size");

  std::vector<DeviceVector<word>> tmp;
  std::vector<DvView<word>> tmp_view;
  std::vector<DvConstView<word>> tmp_const_view;
  std::vector<DvConstView<word>> dst_const_view;
  for (int i = 0; i < num_poly; i++) {
    tmp.emplace_back(dst.at(i).TotalSize());
    tmp_view.push_back(tmp.at(i).View(dst.at(i).AuxSize()));
    tmp_const_view.push_back(tmp.at(i).ConstView(dst.at(i).AuxSize()));
    dst_const_view.emplace_back(dst.at(i));
  }

  // first iteration
  int per = permute_amounts.at(0);
  const std::vector<DvConstView<word>> *prev_accum = &(srcs.at(0));
  if (per != 0) {
    if (has_extra_ct) {
      Permute(tmp_view, np, per, srcs.at(0));
      Add(dst, np, tmp_const_view, srcs.back());
    } else {
      Permute(tmp_view, np, per, srcs.at(0));
    }
    prev_accum = &dst_const_view;
  } else {
    if (has_extra_ct) {
      Add(dst, np, *prev_accum, srcs.back());
      prev_accum = &dst_const_view;
    }
  }
  for (int i = 1; i < num_accum; i++) {
    per = permute_amounts.at(i);
    if (per != 0) {
      Permute(tmp_view, np, per, srcs.at(i));
      Add(dst, np, tmp_const_view, *prev_accum);
    } else {
      Add(dst, np, srcs.at(i), *prev_accum);
    }
    prev_accum = &dst_const_view;
  }
}

template <typename word>
void ElementWiseHandler<word>::PermuteAccumWorker(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<int> &permute_amounts,
    const std::vector<std::vector<DvConstView<word>>> &srcs) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "PermuteAccum: Invalid number of polynomials");

  int num_accum = permute_amounts.size();

  if (num_accum > max_num_accum_) {
    // Prepare temporary result DV
    std::vector<DeviceVector<word>> temp;
    std::vector<DvView<word>> temp_view;
    std::vector<DvConstView<word>> temp_const_view;
    for (int i = 0; i < num_poly; i++) {
      temp.emplace_back(dst.at(i).TotalSize());
      temp_view.push_back(temp.at(i).View(dst.at(i).AuxSize()));
      temp_const_view.push_back(temp.at(i).ConstView(dst.at(i).AuxSize()));
    }

    // Split the srcs/permute_amounts into front and back
    std::vector<std::vector<DvConstView<word>>> srcs_front(
        srcs.begin(), srcs.begin() + max_num_accum_);
    srcs_front.push_back(temp_const_view);
    std::vector<std::vector<DvConstView<word>>> srcs_back(
        srcs.begin() + max_num_accum_, srcs.end());
    std::vector<int> permute_front(permute_amounts.begin(),
                                   permute_amounts.begin() + max_num_accum_);
    std::vector<int> permute_back(permute_amounts.begin() + max_num_accum_,
                                  permute_amounts.end());

    // Recursive call to accumulate the front and back
    PermuteAccum(temp_view, np, permute_back, srcs_back);
    PermuteAccum(dst, np, permute_front, srcs_front);
    return;
  }

  // Some sanity checks
  int num_ct = srcs.size();
  bool has_extra_ct = (num_ct == num_accum + 1);

  AssertTrue(num_accum > 0 && (num_accum == num_ct || has_extra_ct),
             "PermuteAccum: Invalid number of accumulations");
  for (int i = 0; i < num_accum; i++) {
    const auto &src = srcs.at(i);
    AssertTrue(num_poly == static_cast<int>(src.size()),
               "PermuteAccum: Incompatible dst/src size");
    for (int j = 0; j < num_poly; j++) {
      AssertTrue(dst.at(j).data() != src.at(j).data(),
                 "PermuteAccum does not support inplace operation");
    }
  }
  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  int half_degree = param_.degree_ / 2;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;

    // Prepare PtrList objects
    OutputPtrList<word, j> dst_ptr_list(dst);
    std::vector<PermuteInputPtrList<word, j>> src_ptr_list;
    for (int i = 0; i < num_accum; i++) {
      const auto &src_i = srcs.at(i);
      src_ptr_list.emplace_back(src_i);
      src_ptr_list.back().extra_ = src_i.at(0).QSize() - q_size;
      src_ptr_list.back().galois_factor_ =
          PermuteAmountToGaloisFactor(permute_amounts.at(i));
    }

    if (has_extra_ct) {
      InputPtrList<word, j> extra_ct(srcs.back());
      extra_ct.extra_ = srcs.back().at(0).QSize() - q_size;

      // Hard-coded kernel launch
      switch (num_accum) {
        case 1:
          kernel::PermuteAccumAdd<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, extra_ct, src_ptr_list[0]);
          break;
        case 2:
          kernel::PermuteAccumAdd<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, extra_ct, src_ptr_list[0],
              src_ptr_list[1]);
          break;
        case 3:
          kernel::PermuteAccumAdd<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, extra_ct, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2]);
          break;
        case 4:
          kernel::PermuteAccumAdd<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, extra_ct, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2], src_ptr_list[3]);
          break;
        case 5:
          kernel::PermuteAccumAdd<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, extra_ct, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2], src_ptr_list[3],
              src_ptr_list[4]);
          break;
        case 6:
          kernel::PermuteAccumAdd<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, extra_ct, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2], src_ptr_list[3],
              src_ptr_list[4], src_ptr_list[5]);
          break;
        case 7:
          kernel::PermuteAccumAdd<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, extra_ct, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2], src_ptr_list[3],
              src_ptr_list[4], src_ptr_list[5], src_ptr_list[6]);
          break;
        case 8:
          kernel::PermuteAccumAdd<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, extra_ct, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2], src_ptr_list[3],
              src_ptr_list[4], src_ptr_list[5], src_ptr_list[6],
              src_ptr_list[7]);
          break;
        default:
          Fail("PermuteAccum: Invalid number of accumulations");
          break;
      }
    } else {
      // Hard-coded kernel launch
      switch (num_accum) {
        case 1:
          kernel::PermuteAccum<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, src_ptr_list[0]);
          break;
        case 2:
          kernel::PermuteAccum<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
              src_ptr_list[1]);
          break;
        case 3:
          kernel::PermuteAccum<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2]);
          break;
        case 4:
          kernel::PermuteAccum<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2], src_ptr_list[3]);
          break;
        case 5:
          kernel::PermuteAccum<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2], src_ptr_list[3],
              src_ptr_list[4]);
          break;
        case 6:
          kernel::PermuteAccum<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2], src_ptr_list[3],
              src_ptr_list[4], src_ptr_list[5]);
          break;
        case 7:
          kernel::PermuteAccum<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2], src_ptr_list[3],
              src_ptr_list[4], src_ptr_list[5], src_ptr_list[6]);
          break;
        case 8:
          kernel::PermuteAccum<word, j><<<grid_dim, kernel_block_dim_>>>(
              dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
              src_ptr_list[1], src_ptr_list[2], src_ptr_list[3],
              src_ptr_list[4], src_ptr_list[5], src_ptr_list[6],
              src_ptr_list[7]);
          break;
        default:
          Fail("PermuteAccum: Invalid number of accumulations");
          break;
      }
    }
  });
}

template <typename word>
void ElementWiseHandler<word>::Accum(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<std::vector<DvConstView<word>>> &srcs) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "Accum: Invalid number of polynomials");

  int num_accum = srcs.size();

  if (num_accum > max_num_accum_) {
    // Prepare temporary result DV
    std::vector<DeviceVector<word>> temp;
    std::vector<DvView<word>> temp_view;
    std::vector<DvConstView<word>> temp_const_view;
    for (int i = 0; i < num_poly; i++) {
      temp.emplace_back(dst.at(i).TotalSize());
      temp_view.push_back(temp.at(i).View(dst.at(i).AuxSize()));
      temp_const_view.push_back(temp.at(i).ConstView(dst.at(i).AuxSize()));
    }

    // Split the srcs/permute_amounts into front and back
    std::vector<std::vector<DvConstView<word>>> srcs_front(
        srcs.begin(), srcs.begin() + max_num_accum_ - 1);
    srcs_front.push_back(temp_const_view);
    std::vector<std::vector<DvConstView<word>>> srcs_back(
        srcs.begin() + max_num_accum_ - 1, srcs.end());

    // Recursive call to accumulate the front and back
    Accum(temp_view, np, srcs_back);
    Accum(dst, np, srcs_front);
    return;
  }

  // Some sanity checks
  AssertTrue(num_accum > 1, "Accum: Invalid number of accumulations");
  for (int i = 0; i < num_accum; i++) {
    const auto &src = srcs.at(i);
    AssertTrue(num_poly == static_cast<int>(src.size()),
               "Accum: Incompatible dst/src size");
  }

  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;

    // Preparing PtrList objects
    OutputPtrList<word, j> dst_ptr_list(dst);
    std::vector<InputPtrList<word, j>> src_ptr_list;
    for (int i = 0; i < num_accum; i++) {
      const auto &src_i = srcs.at(i);
      src_ptr_list.emplace_back(src_i);
      src_ptr_list.back().extra_ = src_i.at(0).QSize() - q_size;
    }

    // Hard-coded kernel launch
    switch (num_accum) {
      case 2:
        kernel::Sum<word, j><<<grid_dim, kernel_block_dim_>>>(
            dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
            src_ptr_list[1]);
        break;
      case 3:
        kernel::Sum<word, j><<<grid_dim, kernel_block_dim_>>>(
            dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
            src_ptr_list[1], src_ptr_list[2]);
        break;
      case 4:
        kernel::Sum<word, j><<<grid_dim, kernel_block_dim_>>>(
            dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
            src_ptr_list[1], src_ptr_list[2], src_ptr_list[3]);
        break;
      case 5:
        kernel::Sum<word, j><<<grid_dim, kernel_block_dim_>>>(
            dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
            src_ptr_list[1], src_ptr_list[2], src_ptr_list[3], src_ptr_list[4]);
        break;
      case 6:
        kernel::Sum<word, j><<<grid_dim, kernel_block_dim_>>>(
            dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
            src_ptr_list[1], src_ptr_list[2], src_ptr_list[3], src_ptr_list[4],
            src_ptr_list[5]);
        break;
      case 7:
        kernel::Sum<word, j><<<grid_dim, kernel_block_dim_>>>(
            dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
            src_ptr_list[1], src_ptr_list[2], src_ptr_list[3], src_ptr_list[4],
            src_ptr_list[5], src_ptr_list[6]);
        break;
      case 8:
        kernel::Sum<word, j><<<grid_dim, kernel_block_dim_>>>(
            dst_ptr_list, primes, num_q_primes, src_ptr_list[0],
            src_ptr_list[1], src_ptr_list[2], src_ptr_list[3], src_ptr_list[4],
            src_ptr_list[5], src_ptr_list[6], src_ptr_list[7]);
        break;
      default:
        Fail("Accum: Invalid number of accumulations");
        break;
    }
  });
}

template <typename word>
void ElementWiseHandler<word>::CAccum(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<std::vector<DvConstView<word>>> &ct_srcs,
    const std::vector<DvConstView<word>> &common_srcs) const {
  if (kMergeCMult) {
    CPAccumWorker<true>(dst, np, ct_srcs, common_srcs);
    return;
  }

  // Naive repetition of CMult and add
  int num_poly = dst.size();
  int num_accum = common_srcs.size();
  bool has_extra_ct = (ct_srcs.size() == (common_srcs.size() + 1));
  AssertTrue(num_accum == static_cast<int>(ct_srcs.size()) || has_extra_ct,
             "CPAccum: Incompatible ct_srcs/common_srcs size");

  std::vector<DeviceVector<word>> tmp;
  std::vector<DvView<word>> tmp_view;
  std::vector<DvConstView<word>> tmp_const_view;
  std::vector<DvConstView<word>> dst_const_view;
  for (int i = 0; i < num_poly; i++) {
    tmp.emplace_back(dst.at(i).TotalSize());
    tmp_view.push_back(tmp.at(i).View(dst.at(i).AuxSize()));
    tmp_const_view.push_back(tmp.at(i).ConstView(dst.at(i).AuxSize()));
    dst_const_view.emplace_back(dst.at(i));
  }

  // first iteration
  if (has_extra_ct) {
    MultConst(tmp_view, np, ct_srcs.at(0), common_srcs.at(0));
    Add(dst, np, tmp_const_view, ct_srcs.back());
  } else {
    MultConst(dst, np, ct_srcs.at(0), common_srcs.at(0));
  }
  for (int i = 1; i < num_accum; i++) {
    MultConst(tmp_view, np, ct_srcs.at(i), common_srcs.at(i));
    Add(dst, np, tmp_const_view, dst_const_view);
  }
}

template <typename word>
void ElementWiseHandler<word>::PAccum(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<std::vector<DvConstView<word>>> &ct_srcs,
    const std::vector<DvConstView<word>> &common_srcs) const {
  if (kMergePMult) {
    CPAccumWorker<false>(dst, np, ct_srcs, common_srcs);
    return;
  }

  // Naive repetition of PMult and add
  int num_poly = dst.size();
  int num_accum = common_srcs.size();
  bool has_extra_ct = (ct_srcs.size() == (common_srcs.size() + 1));
  AssertTrue(num_accum == static_cast<int>(ct_srcs.size()) || has_extra_ct,
             "CPAccum: Incompatible ct_srcs/common_srcs size");

  std::vector<DeviceVector<word>> tmp;
  std::vector<DvView<word>> tmp_view;
  std::vector<DvConstView<word>> tmp_const_view;
  std::vector<DvConstView<word>> dst_const_view;
  for (int i = 0; i < num_poly; i++) {
    tmp.emplace_back(dst.at(i).TotalSize());
    tmp_view.push_back(tmp.at(i).View(dst.at(i).AuxSize()));
    tmp_const_view.push_back(tmp.at(i).ConstView(dst.at(i).AuxSize()));
    dst_const_view.emplace_back(dst.at(i));
  }

  // first iteration
  if (has_extra_ct) {
    PMult(tmp_view, np, ct_srcs.at(0), common_srcs.at(0));
    Add(dst, np, tmp_const_view, ct_srcs.back());
  } else {
    PMult(dst, np, ct_srcs.at(0), common_srcs.at(0));
  }
  for (int i = 1; i < num_accum; i++) {
    PMult(tmp_view, np, ct_srcs.at(i), common_srcs.at(i));
    Add(dst, np, tmp_const_view, dst_const_view);
  }
}

template <typename word>
template <bool const_accum>
void ElementWiseHandler<word>::CPAccumWorker(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<std::vector<DvConstView<word>>> &ct_srcs,
    const std::vector<DvConstView<word>> &common_srcs) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "CPAccum: Invalid number of polynomials");

  int num_accum = common_srcs.size();
  bool has_extra_ct = (ct_srcs.size() == (common_srcs.size() + 1));
  AssertTrue(num_accum == static_cast<int>(ct_srcs.size()) || has_extra_ct,
             "CPAccum: Incompatible ct_srcs/common_srcs size");

  if (num_accum > max_num_accum_) {
    // Prepare temporary result DV
    std::vector<DeviceVector<word>> temp;
    std::vector<DvView<word>> temp_view;
    std::vector<DvConstView<word>> temp_const_view;
    for (int i = 0; i < num_poly; i++) {
      int aux_size = dst.at(i).AuxSize();
      temp.emplace_back(dst.at(i).TotalSize());
      temp_view.push_back(temp.at(i).View(aux_size));
      temp_const_view.push_back(temp.at(i).ConstView(aux_size));
    }

    // Split the ct_srcs/common_srcs into front and back
    std::vector<std::vector<DvConstView<word>>> ct_srcs_front(
        ct_srcs.begin(), ct_srcs.begin() + max_num_accum_);
    ct_srcs_front.push_back(temp_const_view);
    std::vector<std::vector<DvConstView<word>>> ct_srcs_back(
        ct_srcs.begin() + max_num_accum_, ct_srcs.end());
    std::vector<DvConstView<word>> common_srcs_front(
        common_srcs.begin(), common_srcs.begin() + max_num_accum_);
    std::vector<DvConstView<word>> common_srcs_back(
        common_srcs.begin() + max_num_accum_, common_srcs.end());

    // Recursive call to accumulate the front and back
    CPAccumWorker<const_accum>(temp_view, np, ct_srcs_back, common_srcs_back);
    CPAccumWorker<const_accum>(dst, np, ct_srcs_front, common_srcs_front);
    return;
  }

  // Some sanity checks
  AssertTrue(num_accum > 0, "CPAccum: Invalid number of accumulations");
  for (const auto &ct_src : ct_srcs) {
    AssertTrue(num_poly == static_cast<int>(ct_src.size()),
               "CPAccum: Incompatible dst/ct_src size");
  }

  AssertNPMatch(dst, np);

  const word *primes = param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = param_.GetInvPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;

    // Preparing PtrList objects
    OutputPtrList<word, j> dst_ptr_list(dst);
    std::vector<CPAccumInputPtrList<word, j>> src_ptr_list;
    for (int i = 0; i < num_accum; i++) {
      const auto &ct_src_i = ct_srcs.at(i);
      const auto &common_src_i = common_srcs.at(i);
      src_ptr_list.emplace_back(ct_src_i, common_src_i);
      src_ptr_list.back().extra_ = ct_src_i.at(0).QSize() - q_size;
      src_ptr_list.back().common_extra_ = common_src_i.QSize();
      if constexpr (const_accum) {  // CAccum
        src_ptr_list.back().common_extra_ -= num_q_primes;
      } else {  // PAccum
        src_ptr_list.back().common_extra_ -= q_size;
      }
    }

    // Hard-coded kernel launch
    if (has_extra_ct) {
      InputPtrList<word, j> src0(ct_srcs.back());
      src0.extra_ = ct_srcs.back().at(0).QSize() - q_size;
      switch (num_accum) {
        case 1:
          kernel::CPAccumAdd<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(dst_ptr_list, primes,
                                                inv_primes, num_q_primes, src0,
                                                src_ptr_list[0]);
          break;
        case 2:
          kernel::CPAccumAdd<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes, src0,
                  src_ptr_list[0], src_ptr_list[1]);
          break;
        case 3:
          kernel::CPAccumAdd<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes, src0,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2]);
          break;
        case 4:
          kernel::CPAccumAdd<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes, src0,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2],
                  src_ptr_list[3]);
          break;
        case 5:
          kernel::CPAccumAdd<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes, src0,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2],
                  src_ptr_list[3], src_ptr_list[4]);
          break;
        case 6:
          kernel::CPAccumAdd<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes, src0,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2],
                  src_ptr_list[3], src_ptr_list[4], src_ptr_list[5]);
          break;
        case 7:
          kernel::CPAccumAdd<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes, src0,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2],
                  src_ptr_list[3], src_ptr_list[4], src_ptr_list[5],
                  src_ptr_list[6]);
          break;
        case 8:
          kernel::CPAccumAdd<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes, src0,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2],
                  src_ptr_list[3], src_ptr_list[4], src_ptr_list[5],
                  src_ptr_list[6], src_ptr_list[7]);
          break;
        default:
          Fail("CPAccum: Invalid number of accumulations");
          break;
      }
    } else {
      switch (num_accum) {
        case 1:
          kernel::CPAccum<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(dst_ptr_list, primes,
                                                inv_primes, num_q_primes,
                                                src_ptr_list[0]);
          break;
        case 2:
          kernel::CPAccum<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes,
                  src_ptr_list[0], src_ptr_list[1]);
          break;
        case 3:
          kernel::CPAccum<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2]);
          break;
        case 4:
          kernel::CPAccum<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2],
                  src_ptr_list[3]);
          break;
        case 5:
          kernel::CPAccum<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2],
                  src_ptr_list[3], src_ptr_list[4]);
          break;
        case 6:
          kernel::CPAccum<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2],
                  src_ptr_list[3], src_ptr_list[4], src_ptr_list[5]);
          break;
        case 7:
          kernel::CPAccum<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2],
                  src_ptr_list[3], src_ptr_list[4], src_ptr_list[5],
                  src_ptr_list[6]);
          break;
        case 8:
          kernel::CPAccum<word, j, const_accum>
              <<<grid_dim, kernel_block_dim_>>>(
                  dst_ptr_list, primes, inv_primes, num_q_primes,
                  src_ptr_list[0], src_ptr_list[1], src_ptr_list[2],
                  src_ptr_list[3], src_ptr_list[4], src_ptr_list[5],
                  src_ptr_list[6], src_ptr_list[7]);
          break;
        default:
          Fail("CPAccum: Invalid number of accumulations");
          break;
      }
    }
  });
}

template <typename word>
void ElementWiseHandler<word>::ModUpToMax(DvView<word> &dst,
                                          const DvConstView<word> &src1) const {
  int num_aux = dst.AuxSize() / param_.degree_;
  ModUpToMax(dst, src1, param_.LevelToNP(param_.max_level_, num_aux));
}

template <typename word>
void ElementWiseHandler<word>::ModUpToMax(DvView<word> &dst,
                                          const DvConstView<word> &src1,
                                          const NPInfo &max_np) const {
  NPInfo min_np = param_.LevelToNP(-1);
  std::vector<word> base_primes = param_.GetPrimeVector(min_np);
  auto dst_temp = std::vector<DvView<word>>{dst};
  AssertNPMatch(dst_temp, max_np);

  const word *primes = param_.GetPrimesPtr(max_np);
  int num_q_primes = max_np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = max_np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  if (base_primes.size() == 2) {
    kernel::ModUpToMax2<word><<<grid_dim, kernel_block_dim_>>>(
        dst.data(), primes, base_primes.at(0), base_primes.at(1), src1.data());
  } else if (base_primes.size() == 1) {
    kernel::ModUpToMax1<word><<<grid_dim, kernel_block_dim_>>>(
        dst.data(), primes, base_primes.at(0), src1.data());
  } else {
    Fail("ModUpToMax: Invalid base primes size");
  }
}

template <typename word>
void ElementWiseHandler<word>::MultImaginaryUnit(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &src1,
    const DvConstView<word> &src_i_unit) const {
  // Check the size of the vectors
  int num_poly = dst.size();
  AssertTrue(num_poly == static_cast<int>(src1.size()),
             "MultImaginaryUnit: Incompatible dst/src size");
  AssertTrue(num_poly > 0 && num_poly <= max_num_poly_,
             "MultImaginaryUnit: Invalid number of polynomials");
  AssertNPMatch({dst}, np);

  const word *primes = param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = param_.GetInvPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  constexpr_for<1, max_num_poly_ + 1>([&](auto j) {
    if (num_poly != j) return;
    OutputPtrList<word, j> dst_ptr_list(dst);
    InputPtrList<word, j> src1_ptr_list(src1);
    src1_ptr_list.extra_ = src1.at(0).QSize() - q_size;
    InputPtrList<word, 1> i_unit(src_i_unit);
    i_unit.extra_ = src_i_unit.QSize() - q_size;

    kernel::MultImaginaryUnit<word, j><<<grid_dim, kernel_block_dim_>>>(
        dst_ptr_list, primes, inv_primes, num_q_primes, src1_ptr_list, i_unit);
  });
}

template <typename word>
void ElementWiseHandler<word>::KeySwitchPAccumBatch(
    std::vector<std::vector<DvView<word>>> &dst_batch, const NPInfo &np,
    const std::vector<std::vector<DvConstView<word>>> &key_views,
    const std::vector<std::vector<DvConstView<word>>> &modup_batch,
    const std::vector<std::vector<DvConstView<word>>> &orig_batch,
    const DvConstView<word> &p_prod) const {
  int total_batch = dst_batch.size();
  int num_chunks = key_views.size();
  AssertTrue(total_batch > 0 && num_chunks > 0 && num_chunks <= 6 &&
                 static_cast<int>(modup_batch.size()) == total_batch &&
                 static_cast<int>(orig_batch.size()) == total_batch,
             "KeySwitchPAccumBatch: invalid batch or chunk sizes");

  const word *primes = param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = param_.GetInvPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  for (int offset = 0; offset < total_batch; offset += 6) {
    int B = std::min(6, total_batch - offset);
    kernel::KeySwitchPAccumBatchParams<word, 6, 6> params{};
    params.num_chunks = num_chunks;
    params.p_prod = p_prod.data();
    for (int m = 0; m < num_chunks; ++m) {
      AssertTrue(key_views[m].size() == 2,
                 "KeySwitchPAccumBatch: key_views must have 2 polynomials");
      params.key_bx[m] = key_views[m][0].data();
      params.key_ax[m] = key_views[m][1].data();
      params.key_extra[m] = key_views[m][0].QSize() - q_size;
    }
    for (int b = 0; b < B; ++b) {
      int idx = offset + b;
      AssertNPMatch(dst_batch[idx], np);
      AssertTrue(dst_batch[idx].size() == 2 &&
                     static_cast<int>(modup_batch[idx].size()) == num_chunks,
                 "KeySwitchPAccumBatch: dst/modup size mismatch");
      params.dst_bx[b] = dst_batch[idx][0].data();
      params.dst_ax[b] = dst_batch[idx][1].data();
      for (int m = 0; m < num_chunks; ++m) {
        params.modup[b][m] = modup_batch[idx][m].data();
        params.modup_extra[b][m] = modup_batch[idx][m].QSize() - q_size;
      }
      params.orig_bx[b] =
          orig_batch[idx].size() >= 1 ? orig_batch[idx][0].data() : nullptr;
      params.orig_ax[b] =
          orig_batch[idx].size() >= 2 ? orig_batch[idx][1].data() : nullptr;
    }
    constexpr_for<1, 7>([&](auto b_const) {
      if (B == b_const) {
        kernel::KeySwitchPAccumBatch<word, b_const>
            <<<grid_dim, kernel_block_dim_>>>(params, primes, inv_primes,
                                              num_q_primes);
      }
    });
  }
}

template <typename word>
void ElementWiseHandler<word>::MultiCAccum(
    std::vector<std::vector<DvView<word>>> &dst_batch, const NPInfo &np,
    const std::vector<std::vector<DvConstView<word>>> &shared_ct_srcs,
    const std::vector<std::vector<DvConstView<word>>> &const_srcs_batch,
    const std::vector<std::vector<DvConstView<word>>> &init_ct_batch,
    const std::vector<DvConstView<word>> &c0_consts_batch) const {
  int total_out = dst_batch.size();
  int total_basis = shared_ct_srcs.size();
  AssertTrue(total_out > 0 && total_basis > 0 &&
                 static_cast<int>(const_srcs_batch.size()) == total_out &&
                 (init_ct_batch.empty() ||
                  static_cast<int>(init_ct_batch.size()) == total_out) &&
                 (c0_consts_batch.empty() ||
                  static_cast<int>(c0_consts_batch.size()) == total_out),
             "MultiCAccum: invalid batch sizes");

  const word *primes = param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = param_.GetInvPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  for (int out_offset = 0; out_offset < total_out; out_offset += 6) {
    int M = std::min(6, total_out - out_offset);
    for (int k_offset = 0; k_offset < total_basis; k_offset += 8) {
      int K = std::min(8, total_basis - k_offset);
      kernel::MultiCAccumParams<word, 6, 8> params{};
      params.num_basis = K;
      params.has_init = (k_offset == 0) ? !init_ct_batch.empty() : true;
      for (int k = 0; k < K; ++k) {
        const auto &ct_src = shared_ct_srcs[k_offset + k];
        AssertTrue(ct_src.size() == 2,
                   "MultiCAccum: shared_ct_srcs must have 2 polynomials");
        params.ct_bx[k] = ct_src[0].data();
        params.ct_ax[k] = ct_src[1].data();
        params.ct_extra[k] = ct_src[0].QSize() - q_size;
      }
      for (int m = 0; m < M; ++m) {
        int out_idx = out_offset + m;
        AssertNPMatch(dst_batch[out_idx], np);
        AssertTrue(dst_batch[out_idx].size() == 2 &&
                       static_cast<int>(const_srcs_batch[out_idx].size()) ==
                           total_basis,
                   "MultiCAccum: dst/const size mismatch");
        params.dst_bx[m] = dst_batch[out_idx][0].data();
        params.dst_ax[m] = dst_batch[out_idx][1].data();
        if (k_offset == 0 && !init_ct_batch.empty()) {
          params.init_bx[m] = init_ct_batch[out_idx][0].data();
          params.init_ax[m] = init_ct_batch[out_idx][1].data();
          params.init_extra[m] = init_ct_batch[out_idx][0].QSize() - q_size;
        } else if (k_offset > 0) {
          params.init_bx[m] = dst_batch[out_idx][0].data();
          params.init_ax[m] = dst_batch[out_idx][1].data();
          params.init_extra[m] = dst_batch[out_idx][0].QSize() - q_size;
        }
        if (k_offset == 0 && !c0_consts_batch.empty() &&
            c0_consts_batch[out_idx].data() != nullptr) {
          const auto &c0_view = c0_consts_batch[out_idx];
          params.c0_ptrs[m] = c0_view.data();
          params.c0_extra[m] = c0_view.QSize() - num_q_primes;
        } else {
          params.c0_ptrs[m] = nullptr;
          params.c0_extra[m] = 0;
        }
        for (int k = 0; k < K; ++k) {
          const auto &c_view = const_srcs_batch[out_idx][k_offset + k];
          params.const_ptrs[m][k] = c_view.data();
          params.const_extra[m][k] =
              c_view.data() != nullptr ? (c_view.QSize() - num_q_primes) : 0;
        }
      }
      constexpr_for<1, 7>([&](auto m_const) {
        if (M == m_const) {
          kernel::MultiCAccum<word, m_const><<<grid_dim, kernel_block_dim_>>>(
              params, primes, inv_primes, num_q_primes);
        }
      });
    }
  }
}

template <typename word>
void ElementWiseHandler<word>::TensorAXYPBZ(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &x_src,
    const std::vector<DvConstView<word>> &y_src,
    const std::vector<DvConstView<word>> &z_src, const DvConstView<word> &a_const,
    const DvConstView<word> &b_const) const {
  AssertTrue(dst.size() == 3 && x_src.size() == 2 && y_src.size() == 2 &&
                 (z_src.empty() || z_src.size() == 2),
             "TensorAXYPBZ: invalid polynomial sizes");
  AssertNPMatch(dst, np);
  const word *primes = param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = param_.GetInvPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  OutputPtrList<word, 3> dst_ptr_list(dst);
  InputPtrList<word, 2> x_ptr_list(x_src);
  x_ptr_list.extra_ = x_src[0].QSize() - q_size;
  InputPtrList<word, 2> y_ptr_list(y_src);
  y_ptr_list.extra_ = y_src[0].QSize() - q_size;
  InputPtrList<word, 2> z_ptr_list(z_src.empty() ? x_src : z_src);
  z_ptr_list.extra_ = z_src.empty() ? 0 : (z_src[0].QSize() - q_size);

  const word *a_ptr = a_const.data();
  int a_extra = a_ptr != nullptr ? (a_const.QSize() - num_q_primes) : 0;
  const word *b_ptr = b_const.data();
  int b_extra = b_ptr != nullptr ? (b_const.QSize() - num_q_primes) : 0;
  bool is_square_no_a = (a_ptr == nullptr && x_src[0].data() == y_src[0].data() &&
                         x_src[1].data() == y_src[1].data());

  kernel::TensorAXYPBZ<word><<<grid_dim, kernel_block_dim_>>>(
      dst_ptr_list, primes, inv_primes, num_q_primes, x_ptr_list, y_ptr_list,
      z_ptr_list, !z_src.empty(), a_ptr, a_extra, b_ptr, b_extra,
      is_square_no_a);
}

template <typename word>
void ElementWiseHandler<word>::TensorLinearFactor(
    std::vector<DvView<word>> &dst, const NPInfo &np,
    const std::vector<DvConstView<word>> &square_src,
    const std::vector<DvConstView<word>> &x_src,
    const DvConstView<word> &slope_const,
    const DvConstView<word> &offset_const) const {
  AssertTrue(dst.size() == 3 && square_src.size() == 2 && x_src.size() == 2,
             "TensorLinearFactor: invalid polynomial sizes");
  AssertNPMatch(dst, np);
  const word *primes = param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = param_.GetInvPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  OutputPtrList<word, 3> dst_ptr_list(dst);
  InputPtrList<word, 2> sq_ptr_list(square_src);
  sq_ptr_list.extra_ = square_src[0].QSize() - q_size;
  InputPtrList<word, 2> x_ptr_list(x_src);
  x_ptr_list.extra_ = x_src[0].QSize() - q_size;

  int slope_extra = slope_const.QSize() - num_q_primes;
  int offset_extra = offset_const.QSize() - num_q_primes;

  kernel::TensorLinearFactor<word><<<grid_dim, kernel_block_dim_>>>(
      dst_ptr_list, primes, inv_primes, num_q_primes, sq_ptr_list, x_ptr_list,
      slope_const.data(), slope_extra, offset_const.data(), offset_extra);
}

template <typename word>
void ElementWiseHandler<word>::TensorSharedY(
    std::vector<std::vector<DvView<word>>> &dst_batch, const NPInfo &np,
    const std::vector<std::vector<DvConstView<word>>> &src1_batch,
    const std::vector<DvConstView<word>> &shared_src2) const {
  AssertTrue(dst_batch.size() == src1_batch.size() && !dst_batch.empty(),
             "TensorSharedY: size mismatch");
  AssertTrue(shared_src2.size() == 2,
             "TensorSharedY: shared_src2 must have 2 polynomials");
  for (size_t b = 0; b < dst_batch.size(); ++b) {
    AssertTrue(dst_batch[b].size() == 3 && src1_batch[b].size() == 2,
               "TensorSharedY: invalid number of polynomials");
    AssertNPMatch(dst_batch[b], np);
  }

  const word *primes = param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = param_.GetInvPrimesPtr(np);
  int num_q_primes = np.GetNumQ();
  int q_size = num_q_primes * param_.degree_;
  int grid_dim = np.GetNumTotal() * param_.degree_ / kernel_block_dim_;

  const size_t total_B = dst_batch.size();
  constexpr size_t kMaxChunk = 6;
  for (size_t offset = 0; offset < total_B; offset += kMaxChunk) {
    size_t chunk_B = std::min(kMaxChunk, total_B - offset);
    kernel::TensorSharedYParams<word, 6> params{};
    params.B = static_cast<int>(chunk_B);
    params.y_ptrs[0] = shared_src2[0].data();
    params.y_ptrs[1] = shared_src2[1].data();
    params.y_extra = shared_src2[0].QSize() - q_size;

    for (size_t b = 0; b < chunk_B; ++b) {
      size_t idx = offset + b;
      params.dst_ptrs[b][0] = dst_batch[idx][0].data();
      params.dst_ptrs[b][1] = dst_batch[idx][1].data();
      params.dst_ptrs[b][2] = dst_batch[idx][2].data();
      params.src1_ptrs[b][0] = src1_batch[idx][0].data();
      params.src1_ptrs[b][1] = src1_batch[idx][1].data();
      params.src1_extra[b] = src1_batch[idx][0].QSize() - q_size;
    }

    kernel::TensorSharedY<word, 6><<<grid_dim, kernel_block_dim_>>>(
        params, primes, inv_primes, num_q_primes);
  }
}

// explicit instantiation
template class ElementWiseHandler<uint32_t>;
template class ElementWiseHandler<uint64_t>;

}  // namespace cheddar
