#include "extension/LCROperator.h"

#include <cuda_runtime.h>

#include "common/Assert.h"
#include "common/Basic.cuh"
#include "common/PrimeUtils.h"

namespace cheddar {

namespace {

template <typename word>
__global__ void LCRProjectKernel(
    word *dst,
    const word *src,
    const word *primes,
    const word *inv_q_L_mont,
    const word *q_mont_inv,
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
    word inv_qL = inv_q_L_mont[i];

    word c_mod_qi = static_cast<word>(abs_c % q_i);
    word diff;
    if (!is_c_negative) {
      diff = basic::Sub<word>(s_i, c_mod_qi, q_i);
    } else {
      diff = basic::Add<word>(s_i, c_mod_qi, q_i);
    }

    // inv_qL is Montgomery encoded; diff is ordinary. The result is ordinary.
    word z_i = basic::MultMontgomery<word>(
        diff, inv_qL, q_i, static_cast<make_signed_t<word>>(q_mont_inv[i]));
    dst[i * degree + j] = z_i;

    if (i == 0) z_0 = z_i;
    if (i == 1) z_1 = z_i;
    if (i == 2) z_2 = z_i;
  }

  // 4. For limb L: project centered integer value of z modulo q_L
  bool is_z_negative = false;
  uint128_t abs_z = 0;
  basic::ReconstructCenteredCRT<word>(
      is_z_negative, abs_z, z_0, z_1, z_2, primes, inv_q0_mod_q1,
      inv_q0q1_mod_q2, L);

  word z_L;
  if (!is_z_negative) {
    z_L = static_cast<word>(abs_z % static_cast<uint128_t>(q_L));
  } else {
    word rem = static_cast<word>(abs_z % static_cast<uint128_t>(q_L));
    z_L = basic::Negate<word>(rem, q_L);
  }
  dst[L * degree + j] = z_L;
}

template <typename word>
__global__ void LCRModUpProjectionKernel(
    word *dst_p,
    const word *src_q,
    const word *p_primes,
    const word *p_mont_R2,
    const word *p_mont_inv,
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
  uint128_t abs_mag = 0;
  basic::ReconstructCenteredCRT<word>(
      is_negative, abs_mag, z_0, z_1, z_2, q_primes, inv_q0_mod_q1,
      inv_q0q1_mod_q2, num_q);

  for (int k = 0; k < alpha; ++k) {
    word p_k = p_primes[k];
    word val;
    if (!is_negative) {
      val = static_cast<word>(abs_mag % static_cast<uint128_t>(p_k));
    } else {
      word rem = static_cast<word>(abs_mag % static_cast<uint128_t>(p_k));
      val = basic::Negate<word>(rem, p_k);
    }
    // Montgomery multiplication by R^2 converts the ordinary residue to val*R.
    dst_p[k * degree + j] = basic::MultMontgomery<word>(
        val, p_mont_R2[k], p_k,
        static_cast<make_signed_t<word>>(p_mont_inv[k]));
  }
}

// Fuse the diagonal products, automorphisms and sums for both branches. The
// gather mapping is identical to ElementWiseHandler::Permute; accumulation is
// entirely in R_QP before ModDown, and in R_Q before the centered division.
template <typename word>
__global__ void LCRDiagonalAccumKernel(
    word *aks_b, word *aks_a, word *plain_b,
    const word *input_a_qp, const word *input_b_q,
    const uint64_t *pointers, const word *galois_factors,
    const word *primes, const word *mont_inverses,
    int num_diagonals, int num_q, int alpha, int log_degree) {
  int degree = 1 << log_degree;
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= (num_q + alpha) * degree) return;
  int limb = i >> log_degree;
  uint32_t position = i & (degree - 1);
  uint32_t root = basic::BitReverse(position, log_degree + 1) + 1;
  word q = primes[limb];
  auto mont_inverse = static_cast<make_signed_t<word>>(mont_inverses[limb]);
  word sum_b = 0, sum_a = 0, sum_plain = 0;
  for (int k = 0; k < num_diagonals; ++k) {
    uint32_t source = basic::BitReverse(
        root * static_cast<uint32_t>(galois_factors[k]) - 1, log_degree + 1);
    int source_index = (limb << log_degree) + source;
    const word *key_b = reinterpret_cast<const word *>(pointers[k]);
    const word *key_a = reinterpret_cast<const word *>(pointers[num_diagonals + k]);
    word a = basic::StreamingLoad(input_a_qp + source_index);
    sum_b = basic::Add(sum_b, basic::MultMontgomery(
        a, basic::StreamingLoad(key_b + source_index), q, mont_inverse), q);
    sum_a = basic::Add(sum_a, basic::MultMontgomery(
        a, basic::StreamingLoad(key_a + source_index), q, mont_inverse), q);
    if (limb < num_q) {
      const word *diagonal = reinterpret_cast<const word *>(pointers[2 * num_diagonals + k]);
      sum_plain = basic::Add(sum_plain, basic::MultMontgomery(
          basic::StreamingLoad(input_b_q + source_index),
          basic::StreamingLoad(diagonal + i), q, mont_inverse), q);
    }
  }
  aks_b[i] = sum_b;
  aks_a[i] = sum_a;
  if (limb < num_q) plain_b[i] = sum_plain;
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
  HostVector<word> h_q_mont_inv(num_q_);
  for (int i = 0; i < num_q_; i++) {
    h_q_primes[i] = all_primes[i];
    h_q_mont_inv[i] = static_cast<word>(primeutil::InvModBase(all_primes[i]));
  }
  q_primes_gpu_.resize(num_q_);
  CopyHostToDevice(q_primes_gpu_, h_q_primes);
  q_mont_inv_gpu_.resize(num_q_);
  CopyHostToDevice(q_mont_inv_gpu_, h_q_mont_inv);

  word q_L = h_q_primes[num_q_ - 1];
  HostVector<word> h_inv_q_L(num_q_ - 1);
  for (int i = 0; i < num_q_ - 1; i++) {
    word q_i = h_q_primes[i];
    word q_L_mod_qi = static_cast<word>(q_L % q_i);
    h_inv_q_L[i] = primeutil::ToMontgomery<word>(
        primeutil::InvMod<word>(q_L_mod_qi, q_i), q_i);
  }
  inv_q_L_mont_gpu_.resize(num_q_ - 1);
  CopyHostToDevice(inv_q_L_mont_gpu_, h_inv_q_L);

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
  HostVector<word> h_p_mont_R2(alpha_);
  HostVector<word> h_p_mont_inv(alpha_);
  for (int k = 0; k < alpha_; k++) {
    word p_k = all_qp_primes[num_q_ + k];
    h_p_primes[k] = p_k;
    h_p_mont_R2[k] = primeutil::ToMontgomery<word>(
        primeutil::ToMontgomery<word>(1, p_k), p_k);
    h_p_mont_inv[k] = static_cast<word>(primeutil::InvModBase(p_k));
  }
  p_primes_gpu_.resize(alpha_);
  CopyHostToDevice(p_primes_gpu_, h_p_primes);
  p_mont_R2_gpu_.resize(alpha_);
  CopyHostToDevice(p_mont_R2_gpu_, h_p_mont_R2);
  p_mont_inv_gpu_.resize(alpha_);
  CopyHostToDevice(p_mont_inv_gpu_, h_p_mont_inv);
  HostVector<word> h_qp_primes(all_qp_primes.begin(), all_qp_primes.end());
  HostVector<word> h_qp_inv(num_q_ + alpha_);
  for (int i = 0; i < num_q_ + alpha_; ++i) {
    h_qp_inv[i] = static_cast<word>(primeutil::InvModBase(h_qp_primes[i]));
  }
  qp_primes_gpu_.resize(num_q_ + alpha_);
  qp_mont_inv_gpu_.resize(num_q_ + alpha_);
  CopyHostToDevice(qp_primes_gpu_, h_qp_primes);
  CopyHostToDevice(qp_mont_inv_gpu_, h_qp_inv);
  HostVector<word> h_galois(supports_.size());
  for (size_t i = 0; i < supports_.size(); ++i) {
    int rotation = supports_[i] % (context->param_.degree_ / 2);
    if (rotation < 0) rotation += context->param_.degree_ / 2;
    h_galois[i] = context->param_.GetGaloisFactor(rotation);
  }
  galois_gpu_.resize(supports_.size());
  CopyHostToDevice(galois_gpu_, h_galois);
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

  auto &mod_switcher = context->mod_switch_handlers_.at(level_);

  // --- Step 1: c1 AKS Key-Switch Branch ---
  // Single-digit centered ModUp projection for c1 to (Q+P)
  DeviceVector<word> c1_modup((num_q + alpha) * degree);

  // Copy Q limbs directly (already in NTT Montgomery domain)
  cudaMemcpy(c1_modup.data(), input.AxConstView().data(),
             num_q * degree * sizeof(word), cudaMemcpyDeviceToDevice);

  // INTT c1 to coefficient domain for exact centered integer projection
  NPInfo q_np(input_np.num_main_, input_np.num_ter_, 0);
  NPInfo qp_np(input_np.num_main_, input_np.num_ter_, alpha);
  DeviceVector<word> c1_coeff(num_q * degree);
  DvView<word> c1_coeff_view = c1_coeff.View(0, 0);
  context->ntt_handler_.INTT(c1_coeff_view, q_np, input.AxConstView(), true);

  // Exact centered projection to P limbs
  int block_dim = 256;
  int grid_dim = degree / block_dim;
  LCRModUpProjectionKernel<word><<<grid_dim, block_dim>>>(
      c1_modup.data() + num_q * degree, c1_coeff.data(),
      p_primes_gpu_.data(), p_mont_R2_gpu_.data(), p_mont_inv_gpu_.data(),
      q_primes_gpu_.data(),
      inv_q0_mod_q1_, inv_q0q1_mod_q2_, num_q, alpha, degree);

  // Forward NTT on the P limbs (skipping Q limbs 0..num_q-1)
  DvView<word> c1_modup_view = c1_modup.View(alpha * degree, 0);
  context->ntt_handler_.NTTForModUp(c1_modup_view, qp_np, 0, num_q,
                                     c1_modup.ConstView(alpha * degree, 0));
  DeviceVector<word> c0_qp_accum((num_q + alpha) * degree);
  DeviceVector<word> c1_qp_accum((num_q + alpha) * degree);
  DeviceVector<word> S0_q(num_q * degree);
  int num_diagonals = supports_.size();
  HostVector<uint64_t> pointers(3 * num_diagonals);
  for (int k = 0; k < num_diagonals; ++k) {
    int rot = supports_[k];
    AssertTrue(aks_map.HasKey(rot),
               "LCROperator: Missing AKS key for rotation " + std::to_string(rot));
    const auto &key = aks_map.GetKey(rot);
    AssertTrue(key.GetNP() == qp_np && key.GetBeta() == 1,
               "LCROperator: AKS key shape mismatch");
    pointers[k] = reinterpret_cast<uint64_t>(key.BxConstView(0).data());
    pointers[num_diagonals + k] = reinterpret_cast<uint64_t>(key.AxConstView(0).data());
    pointers[2 * num_diagonals + k] =
        reinterpret_cast<uint64_t>(diagonals_.at(rot).ConstView(0).data());
  }
  DeviceVector<uint64_t> device_pointers(pointers.size());
  CopyHostToDevice(device_pointers, pointers);
  LCRDiagonalAccumKernel<word><<<DivCeil((num_q + alpha) * degree, 256), 256>>>(
      c0_qp_accum.data(), c1_qp_accum.data(), S0_q.data(),
      c1_modup.data(), input.BxConstView().data(), device_pointers.data(),
      galois_gpu_.data(), qp_primes_gpu_.data(), qp_mont_inv_gpu_.data(),
      num_diagonals, num_q, alpha, context->param_.log_degree_);

  // Divide the AKS branch by P without dropping a Q level.
  DeviceVector<word> c0_aks_q(num_q * degree);
  DeviceVector<word> c1_aks_q(num_q * degree);
  auto c0_view = c0_aks_q.View();
  auto c1_view = c1_aks_q.View();
  mod_switcher.ModDown(c0_view, c0_qp_accum.ConstView(alpha * degree));
  mod_switcher.ModDown(c1_view, c1_qp_accum.ConstView(alpha * degree));

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
      inv_q_L_mont_gpu_.data(), q_mont_inv_gpu_.data(),
      inv_q0_mod_q1_, inv_q0q1_mod_q2_, num_q,
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
