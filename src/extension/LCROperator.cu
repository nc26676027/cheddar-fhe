#include "extension/LCROperator.h"

#include <cuda_runtime.h>

#include "common/Assert.h"
#include "common/Basic.cuh"
#include "common/PrimeUtils.h"

namespace cheddar {

namespace {

template <typename word>
__inline__ __device__ word MultModDevice(word a, word b, word q) {
  auto prod = basic::detail::__mult_wide<word>(a, b);
  return static_cast<word>(prod % q);
}

template <typename word>
__inline__ __device__ void ReconstructCenteredMultiLimb(
    bool &is_negative,
    __uint128_t &abs_mag,
    word z_0,
    word z_1,
    word z_2,
    const word *primes,
    word inv_q0_mod_q1,
    word inv_q0q1_mod_q2,
    int num_available_limbs) {
  if constexpr (sizeof(word) == 8) {
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
    } else {
      __uint128_t q_0 = primes[0];
      __uint128_t q_1 = primes[1];
      __uint128_t z0_mod_q1 = static_cast<__uint128_t>(z_0) % q_1;
      __uint128_t diff = (static_cast<__uint128_t>(z_1) >= z0_mod_q1)
                             ? (static_cast<__uint128_t>(z_1) - z0_mod_q1)
                             : (static_cast<__uint128_t>(z_1) + q_1 - z0_mod_q1);
      __uint128_t v = (diff * static_cast<__uint128_t>(inv_q0_mod_q1)) % q_1;
      __uint128_t z_val = static_cast<__uint128_t>(z_0) + q_0 * v;
      __uint128_t M = q_0 * q_1;
      __uint128_t half_M = M >> 1;
      if (z_val <= half_M) {
        is_negative = false;
        abs_mag = z_val;
      } else {
        is_negative = true;
        abs_mag = M - z_val;
      }
    }
  } else {
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
    } else if (num_available_limbs == 2) {
      uint64_t q_0 = primes[0];
      uint64_t q_1 = primes[1];
      uint64_t z_0_64 = z_0;
      uint64_t z_1_64 = z_1;
      uint64_t z0_mod_q1 = z_0_64 % q_1;
      uint64_t diff = (z_1_64 >= z0_mod_q1) ? (z_1_64 - z0_mod_q1)
                                            : (z_1_64 + q_1 - z0_mod_q1);
      uint64_t v = (diff * static_cast<uint64_t>(inv_q0_mod_q1)) % q_1;
      uint64_t z_val = z_0_64 + q_0 * v;
      uint64_t M = q_0 * q_1;
      uint64_t half_M = M >> 1;
      if (z_val <= half_M) {
        is_negative = false;
        abs_mag = z_val;
      } else {
        is_negative = true;
        abs_mag = M - z_val;
      }
    } else {
      __uint128_t q_0 = primes[0];
      __uint128_t q_1 = primes[1];
      __uint128_t q_2 = primes[2];
      __uint128_t z0_mod_q1 = static_cast<__uint128_t>(z_0) % q_1;
      __uint128_t diff1 = (static_cast<__uint128_t>(z_1) >= z0_mod_q1)
                              ? (static_cast<__uint128_t>(z_1) - z0_mod_q1)
                              : (static_cast<__uint128_t>(z_1) + q_1 - z0_mod_q1);
      __uint128_t v1 = (diff1 * static_cast<__uint128_t>(inv_q0_mod_q1)) % q_1;
      __uint128_t z_01 = static_cast<__uint128_t>(z_0) + q_0 * v1;
      __uint128_t q_01 = q_0 * q_1;

      __uint128_t z01_mod_q2 = z_01 % q_2;
      __uint128_t diff2 = (static_cast<__uint128_t>(z_2) >= z01_mod_q2)
                              ? (static_cast<__uint128_t>(z_2) - z01_mod_q2)
                              : (static_cast<__uint128_t>(z_2) + q_2 - z01_mod_q2);
      __uint128_t v2 = (diff2 * static_cast<__uint128_t>(inv_q0q1_mod_q2)) % q_2;
      __uint128_t z_val = z_01 + q_01 * v2;
      __uint128_t M = q_01 * q_2;
      __uint128_t half_M = M >> 1;
      if (z_val <= half_M) {
        is_negative = false;
        abs_mag = z_val;
      } else {
        is_negative = true;
        abs_mag = M - z_val;
      }
    }
  }
}

template <typename word>
__global__ void LCRProjectKernel(
    word *dst,
    const word *src,
    const word *primes,
    const word *inv_q_L_mod_qi,
    word inv_q0_mod_q1,
    word inv_q0q1_mod_q2,
    int num_q,
    int degree) {
  int j = blockIdx.x * blockDim.x + threadIdx.x;
  if (j >= degree) return;

  int L = num_q - 1;
  word q_L = primes[L];
  word half_q_L = q_L >> 1;

  // 1. Read s_{L, j}
  word s_L = src[L * degree + j];

  // 2. Centered remainder modulo q_L
  bool is_c_negative = (s_L > half_q_L);
  word abs_c = is_c_negative ? (q_L - s_L) : s_L;

  // 3. For each limb i = 0 to L-1: compute (s_i - c_j) * inv_q_L mod q_i
  word z_0 = 0;
  word z_1 = 0;
  word z_2 = 0;
  for (int i = 0; i < L; ++i) {
    word q_i = primes[i];
    word s_i = src[i * degree + j];
    word inv_qL = inv_q_L_mod_qi[i];

    word c_mod_qi = static_cast<word>(abs_c % q_i);
    word diff;
    if (!is_c_negative) {
      diff = (s_i >= c_mod_qi) ? (s_i - c_mod_qi) : (s_i + q_i - c_mod_qi);
    } else {
      word sum = s_i + c_mod_qi;
      diff = (sum >= q_i) ? (sum - q_i) : sum;
    }

    word z_i = MultModDevice<word>(diff, inv_qL, q_i);
    dst[i * degree + j] = z_i;

    if (i == 0) z_0 = z_i;
    if (i == 1) z_1 = z_i;
    if (i == 2) z_2 = z_i;
  }

  // 4. For limb L: project centered integer value of z modulo q_L
  bool is_z_negative = false;
  __uint128_t abs_z = 0;
  ReconstructCenteredMultiLimb<word>(
      is_z_negative, abs_z, z_0, z_1, z_2, primes, inv_q0_mod_q1,
      inv_q0q1_mod_q2, L);

  word z_L;
  if (!is_z_negative) {
    z_L = static_cast<word>(abs_z % static_cast<__uint128_t>(q_L));
  } else {
    word rem = static_cast<word>(abs_z % static_cast<__uint128_t>(q_L));
    z_L = (rem == 0) ? 0 : (q_L - rem);
  }
  dst[L * degree + j] = z_L;
}

template <typename word>
__global__ void CenteredSmallPolyLiftKernel(
    word *dst_p,
    const word *src_q,
    const word *p_primes,
    const word *p_mont_R,
    const word *q_primes,
    word inv_q0_mod_q1,
    word inv_q0q1_mod_q2,
    int num_q,
    int alpha,
    int degree) {
  int j = blockIdx.x * blockDim.x + threadIdx.x;
  if (j >= degree) return;

  word z_0 = src_q[0 * degree + j];
  word z_1 = (num_q > 1) ? src_q[1 * degree + j] : 0;
  word z_2 = (num_q > 2) ? src_q[2 * degree + j] : 0;

  bool is_negative = false;
  __uint128_t abs_mag = 0;
  ReconstructCenteredMultiLimb<word>(
      is_negative, abs_mag, z_0, z_1, z_2, q_primes, inv_q0_mod_q1,
      inv_q0q1_mod_q2, num_q);

  for (int k = 0; k < alpha; ++k) {
    word p_k = p_primes[k];
    word val;
    if (!is_negative) {
      val = static_cast<word>(abs_mag % static_cast<__uint128_t>(p_k));
    } else {
      word rem = static_cast<word>(abs_mag % static_cast<__uint128_t>(p_k));
      val = static_cast<word>((rem == 0) ? 0 : (p_k - rem));
    }
    word mont_R = p_mont_R[k];
    dst_p[k * degree + j] = MultModDevice<word>(val, mont_R, p_k);
  }
}

}  // namespace

template <typename word>
LCROperator<word>::LCROperator(ConstContextPtr<word> context,
                               const StripedMatrix &matrix,
                               int level,
                               double scale)
    : context_(context), level_(level), scale_(scale) {
  NPInfo np = context->param_.LevelToNP(level_, 0);
  num_q_ = np.GetNumQ();

  // 1. Compile plaintexts on device
  for (const auto &[rot, diag] : matrix) {
    supports_.push_back(rot);
    diagonals_.try_emplace(rot, np);
    context->encoder_.Encode(diagonals_.at(rot), level_, scale_, diag);
  }

  // 2. Precompute primes and inv_q_L_mod_qi on host and transfer to device
  auto all_primes = context->param_.GetPrimeVector(np);
  HostVector<word> h_q_primes(num_q_);
  for (int i = 0; i < num_q_; i++) {
    h_q_primes[i] = all_primes[i];
  }
  q_primes_gpu_.resize(num_q_);
  CopyHostToDevice(q_primes_gpu_, h_q_primes);

  word q_L = h_q_primes[num_q_ - 1];
  HostVector<word> h_inv_q_L(num_q_ - 1);
  for (int i = 0; i < num_q_ - 1; i++) {
    word q_i = h_q_primes[i];
    word q_L_mod_qi = static_cast<word>(q_L % q_i);
    h_inv_q_L[i] = primeutil::InvMod<word>(q_L_mod_qi, q_i);
  }
  inv_q_L_mod_qi_gpu_.resize(num_q_ - 1);
  CopyHostToDevice(inv_q_L_mod_qi_gpu_, h_inv_q_L);

  if (num_q_ >= 2) {
    word q0 = h_q_primes[0];
    word q1 = h_q_primes[1];
    inv_q0_mod_q1_ = primeutil::InvMod<word>(static_cast<word>(q0 % q1), q1);
  }
  if (num_q_ >= 3) {
    word q0 = h_q_primes[0];
    word q1 = h_q_primes[1];
    word q2 = h_q_primes[2];
    word q0q1_mod_q2 = primeutil::MultMod<word>(
        static_cast<word>(q0 % q2), static_cast<word>(q1 % q2), q2);
    inv_q0q1_mod_q2_ = primeutil::InvMod<word>(q0q1_mod_q2, q2);
  }

  alpha_ = context->param_.alpha_;
  NPInfo qp_np(np.num_main_, np.num_ter_, alpha_);
  auto all_qp_primes = context->param_.GetPrimeVector(qp_np);
  HostVector<word> h_p_primes(alpha_);
  HostVector<word> h_p_mont_R(alpha_);
  for (int k = 0; k < alpha_; k++) {
    word p_k = all_qp_primes[num_q_ + k];
    h_p_primes[k] = p_k;
    h_p_mont_R[k] = primeutil::ToMontgomery<word>(1, p_k);
  }
  p_primes_gpu_.resize(alpha_);
  CopyHostToDevice(p_primes_gpu_, h_p_primes);
  p_mont_R_gpu_.resize(alpha_);
  CopyHostToDevice(p_mont_R_gpu_, h_p_mont_R);
}

template <typename word>
void LCROperator<word>::Evaluate(Ciphertext<word> &res,
                                 const Ciphertext<word> &input,
                                 const AKSKeyMap<word> &aks_map) const {
  auto context = GetContext();
  NPInfo input_np = input.GetNP();
  int num_q = input_np.GetNumQ();
  AssertTrue(num_q == num_q_, "LCROperator: input level mismatch");

  int alpha = context->param_.alpha_;
  int degree = context->param_.degree_;
  int beta = 1;

  auto &mod_switcher = context->mod_switch_handlers_.at(level_);

  // --- Step 1: c1 AKS Key-Switch Branch ---
  // Centered ModUp for small c1: produces 1 block of (Q+P) with zero CRT extension error.
  std::vector<DeviceVector<word>> c1_modup;
  c1_modup.emplace_back((num_q + alpha) * degree);

  // Copy Q limbs directly (already in NTT Montgomery domain)
  cudaMemcpy(c1_modup[0].data(), input.AxConstView().data(),
             num_q * degree * sizeof(word), cudaMemcpyDeviceToDevice);

  // INTT c1 to coefficient domain for exact centered integer lift
  NPInfo q_np(input_np.num_main_, input_np.num_ter_, 0);
  NPInfo qp_np(input_np.num_main_, input_np.num_ter_, alpha);
  DeviceVector<word> c1_coeff(num_q * degree);
  DvView<word> c1_coeff_view = c1_coeff.View(0, 0);
  context->ntt_handler_.INTT(c1_coeff_view, q_np, input.AxConstView(), true);

  // Exact centered lift to P limbs
  int block_dim = 256;
  int grid_dim = degree / block_dim;
  CenteredSmallPolyLiftKernel<word><<<grid_dim, block_dim>>>(
      c1_modup[0].data() + num_q * degree, c1_coeff.data(),
      p_primes_gpu_.data(), p_mont_R_gpu_.data(), q_primes_gpu_.data(),
      inv_q0_mod_q1_, inv_q0q1_mod_q2_, num_q, alpha, degree);

  // Forward NTT on the P limbs (skipping Q limbs 0..num_q-1)
  DvView<word> c1_modup_view = c1_modup[0].View(alpha * degree, 0);
  context->ntt_handler_.NTTForModUp(c1_modup_view, qp_np, 0, num_q,
                                     c1_modup[0].ConstView(alpha * degree, 0));
  DeviceVector<word> c0_qp_accum((num_q + alpha) * degree);
  DeviceVector<word> c1_qp_accum((num_q + alpha) * degree);
  std::vector<DvView<word>> c0_qp_accum_view{c0_qp_accum.View(alpha * degree, 0)};
  std::vector<DvView<word>> c1_qp_accum_view{c1_qp_accum.View(alpha * degree, 0)};

  DeviceVector<word> tmp0_qp((num_q + alpha) * degree);
  DeviceVector<word> tmp1_qp((num_q + alpha) * degree);
  DeviceVector<word> rot0_qp((num_q + alpha) * degree);
  DeviceVector<word> rot1_qp((num_q + alpha) * degree);
  std::vector<DvView<word>> tmp0_qp_view{tmp0_qp.View(alpha * degree, 0)};
  std::vector<DvView<word>> tmp1_qp_view{tmp1_qp.View(alpha * degree, 0)};
  std::vector<DvView<word>> rot0_qp_view{rot0_qp.View(alpha * degree, 0)};
  std::vector<DvView<word>> rot1_qp_view{rot1_qp.View(alpha * degree, 0)};

  bool first_k = true;
  for (int rot : supports_) {
    AssertTrue(aks_map.HasKey(rot),
               "LCROperator: Missing AKS key for rotation " + std::to_string(rot));
    const auto &evk = aks_map.GetKey(rot);

    // Multiply c1_modup with evk: (c1 * bx, c1 * ax)
    for (int b = 0; b < beta; b++) {
      if (b == 0) {
        context->elem_handler_.Mult(tmp0_qp_view, qp_np,
                                     {c1_modup[b].ConstView(alpha * degree, 0)},
                                     {evk.BxConstView(b)});
        context->elem_handler_.Mult(tmp1_qp_view, qp_np,
                                     {c1_modup[b].ConstView(alpha * degree, 0)},
                                     {evk.AxConstView(b)});
      } else {
        DeviceVector<word> part0((num_q + alpha) * degree);
        DeviceVector<word> part1((num_q + alpha) * degree);
        std::vector<DvView<word>> part0_view{part0.View(alpha * degree, 0)};
        std::vector<DvView<word>> part1_view{part1.View(alpha * degree, 0)};
        context->elem_handler_.Mult(part0_view, qp_np,
                                     {c1_modup[b].ConstView(alpha * degree, 0)},
                                     {evk.BxConstView(b)});
        context->elem_handler_.Mult(part1_view, qp_np,
                                     {c1_modup[b].ConstView(alpha * degree, 0)},
                                     {evk.AxConstView(b)});
        context->elem_handler_.Add(tmp0_qp_view, qp_np,
                                    {tmp0_qp.ConstView(alpha * degree, 0)},
                                    {part0.ConstView(alpha * degree, 0)});
        context->elem_handler_.Add(tmp1_qp_view, qp_np,
                                    {tmp1_qp.ConstView(alpha * degree, 0)},
                                    {part1.ConstView(alpha * degree, 0)});
      }
    }

    // Permute by rot
    int rot_offset = rot % (degree / 2);
    if (rot_offset < 0) rot_offset += (degree / 2);

    const std::vector<DvConstView<word>> *term0_ptr = nullptr;
    const std::vector<DvConstView<word>> *term1_ptr = nullptr;
    std::vector<DvConstView<word>> tmp0_cview{tmp0_qp.ConstView(alpha * degree, 0)};
    std::vector<DvConstView<word>> tmp1_cview{tmp1_qp.ConstView(alpha * degree, 0)};
    std::vector<DvConstView<word>> rot0_cview{rot0_qp.ConstView(alpha * degree, 0)};
    std::vector<DvConstView<word>> rot1_cview{rot1_qp.ConstView(alpha * degree, 0)};

    if (rot_offset == 0) {
      term0_ptr = &tmp0_cview;
      term1_ptr = &tmp1_cview;
    } else {
      context->elem_handler_.Permute(rot0_qp_view, qp_np, rot_offset, tmp0_cview);
      context->elem_handler_.Permute(rot1_qp_view, qp_np, rot_offset, tmp1_cview);
      term0_ptr = &rot0_cview;
      term1_ptr = &rot1_cview;
    }

    if (first_k) {
      cudaMemcpy(c0_qp_accum.data(), (*term0_ptr)[0].data(),
                 (num_q + alpha) * degree * sizeof(word),
                 cudaMemcpyDeviceToDevice);
      cudaMemcpy(c1_qp_accum.data(), (*term1_ptr)[0].data(),
                 (num_q + alpha) * degree * sizeof(word),
                 cudaMemcpyDeviceToDevice);
      first_k = false;
    } else {
      context->elem_handler_.Add(c0_qp_accum_view, qp_np,
                                  {c0_qp_accum.ConstView(alpha * degree, 0)},
                                  *term0_ptr);
      context->elem_handler_.Add(c1_qp_accum_view, qp_np,
                                  {c1_qp_accum.ConstView(alpha * degree, 0)},
                                  *term1_ptr);
    }
  }

  // ModDown (dividing by P without dropping Q level)
  DeviceVector<word> c0_aks_q(num_q * degree);
  DeviceVector<word> c1_aks_q(num_q * degree);
  DvView<word> c0_aks_q_view = c0_aks_q.View(0, 0);
  DvView<word> c1_aks_q_view = c1_aks_q.View(0, 0);
  mod_switcher.ModDown(c0_aks_q_view, c0_qp_accum.ConstView(alpha * degree, 0));
  mod_switcher.ModDown(c1_aks_q_view, c1_qp_accum.ConstView(alpha * degree, 0));

  // --- Step 2: c0 LCR Branch ---
  DeviceVector<word> S0_q(num_q * degree);
  std::vector<DvView<word>> S0_q_view{S0_q.View(0, 0)};
  DeviceVector<word> tmp_q(num_q * degree);
  DeviceVector<word> rot_q(num_q * degree);
  std::vector<DvView<word>> tmp_q_view{tmp_q.View(0, 0)};
  std::vector<DvView<word>> rot_q_view{rot_q.View(0, 0)};

  first_k = true;
  for (int rot : supports_) {
    const auto &m_k = diagonals_.at(rot);
    int rot_offset = rot % (degree / 2);
    if (rot_offset < 0) rot_offset += (degree / 2);

    if (rot_offset == 0) {
      context->elem_handler_.Mult(tmp_q_view, q_np, {input.BxConstView()},
                                   {m_k.ConstView(0)});
    } else {
      context->elem_handler_.Permute(rot_q_view, q_np, rot_offset,
                                      {input.BxConstView()});
      context->elem_handler_.Mult(tmp_q_view, q_np, {rot_q.ConstView(0, 0)},
                                   {m_k.ConstView(0)});
    }

    if (first_k) {
      cudaMemcpy(S0_q.data(), tmp_q.data(), num_q * degree * sizeof(word),
                 cudaMemcpyDeviceToDevice);
      first_k = false;
    } else {
      context->elem_handler_.Add(S0_q_view, q_np, {S0_q.ConstView(0, 0)},
                                  {tmp_q.ConstView(0, 0)});
    }
  }

  // INTT S0_q to coefficient domain
  DeviceVector<word> S0_coeff(num_q * degree);
  DvView<word> S0_coeff_view = S0_coeff.View(0, 0);
  context->ntt_handler_.INTT(S0_coeff_view, q_np, S0_q.ConstView(0, 0), true);

  // LCR Projection Kernel on GPU
  DeviceVector<word> Z_coeff(num_q * degree);
  block_dim = 256;
  grid_dim = degree / block_dim;
  LCRProjectKernel<word><<<grid_dim, block_dim>>>(
      Z_coeff.data(), S0_coeff.data(), q_primes_gpu_.data(),
      inv_q_L_mod_qi_gpu_.data(), inv_q0_mod_q1_, inv_q0q1_mod_q2_, num_q,
      degree);

  // NTT on Z_coeff (with Montgomery conversion)
  DeviceVector<word> Z_ntt(num_q * degree);
  DvView<word> Z_ntt_view = Z_ntt.View(0, 0);
  context->ntt_handler_.NTT(Z_ntt_view, q_np, Z_coeff.ConstView(0, 0), true);

  // Final combination: c0 = c0_aks_q + Z_ntt, c1 = c1_aks_q
  res.ModifyNP(input_np);
  res.SetScale(input.GetScale());
  res.SetNumSlots(input.GetNumSlots());
  std::vector<DvView<word>> res_bx_view{res.BxView()};
  context->elem_handler_.Add(res_bx_view, q_np, {c0_aks_q.ConstView(0, 0)},
                              {Z_ntt.ConstView(0, 0)});
  cudaMemcpy(res.AxView().data(), c1_aks_q.data(),
             num_q * degree * sizeof(word), cudaMemcpyDeviceToDevice);
}

template class LCROperator<uint32_t>;
template class LCROperator<uint64_t>;

}  // namespace cheddar
