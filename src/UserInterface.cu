#include "UserInterface.h"
#include "common/Basic.cuh"
#include "common/CommonUtils.h"
#include "common/ConstantMemory.cuh"
#include "common/PrimeUtils.h"
#include "common/PtrList.h"
#include "core/BigInt.h"

#ifdef ENABLE_EXTENSION
#include "extension/BootContext.h"
#include "extension/StripedMatrix.h"
#endif

namespace cheddar {

namespace kernel {

// dst.ptrs_[0] --> bx (uninitialized)
// dst.ptrs_[1] --> ax (sampled random value)
// bx = -ax * sx + mx + ex
// For this function, we use prime_index modification
template <typename word>
__global__ void Encrypt(OutputPtrList<word, 2> dst, const word *primes,
                        const make_signed_t<word> *inv_primes, int num_q_primes,
                        const InputPtrList<word, 1> sx,
                        const InputPtrList<word, 1> mx,
                        const InputPtrList<word, 1> ex) {
  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);

  int sx_index = i;
  int mx_index = i;
  int ex_index = i;
  if (prime_index >= num_q_primes) {
    sx_index += sx.extra_;
    mx_index += mx.extra_;
    ex_index += ex.extra_;
  }

  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);

  word sx_value = basic::StreamingLoad(sx.ptrs_[0] + sx_index);
  word mx_value = basic::StreamingLoad(mx.ptrs_[0] + mx_index);
  word ex_value = basic::StreamingLoad(ex.ptrs_[0] + ex_index);
  word ax_value = basic::StreamingLoad(dst.ptrs_[1] + i);

  word res = basic::MultMontgomery(ax_value, sx_value, prime, inv_prime);
  res = basic::Sub(mx_value, res, prime);
  res = basic::Add(res, ex_value, prime);

  dst.ptrs_[0][i] = res;
}

// dst.ptrs_[0] --> bx (uninitialized)
// dst.ptrs_[1] --> ax (sampled random value)
// bx = -ax * sx + mx + ex
// For this function, we use prime_index modification
template <typename word>
__global__ void EncryptZero(OutputPtrList<word, 2> dst, const word *primes,
                            const make_signed_t<word> *inv_primes,
                            int num_q_primes, const InputPtrList<word, 1> sx,
                            const InputPtrList<word, 1> ex) {
  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);

  int sx_index = i;
  int ex_index = i;
  if (prime_index >= num_q_primes) {
    sx_index += sx.extra_;
    ex_index += ex.extra_;
  }

  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);

  word sx_value = basic::StreamingLoad(sx.ptrs_[0] + sx_index);
  word ex_value = basic::StreamingLoad(ex.ptrs_[0] + ex_index);
  word ax_value = basic::StreamingLoad(dst.ptrs_[1] + i);

  word res = basic::MultMontgomery(ax_value, sx_value, prime, inv_prime);
  res = basic::Sub(ex_value, res, prime);

  dst.ptrs_[0][i] = res;
}

// Contiguous memory access is guaranteed
template <typename word>
__global__ void AddEvkPart(word *dst, const word *primes,
                           const make_signed_t<word> *inv_primes,
                           const word *src, const word *p_prod) {
  using signed_word = make_signed_t<word>;
  int log_degree = cm_log_degree();
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int prime_index = (i >> log_degree);
  const word prime = basic::StreamingLoadConst(primes + prime_index);
  const signed_word inv_prime =
      basic::StreamingLoadConst(inv_primes + prime_index);
  const word p_prod_value = basic::StreamingLoadConst(p_prod + prime_index);

  word src_value = basic::StreamingLoad(src + i);
  word dst_value = basic::StreamingLoad(dst + i);

  word res = basic::MultMontgomery(src_value, p_prod_value, prime, inv_prime);
  res = basic::Add(dst_value, res, prime);

  dst[i] = res;
}

}  // namespace kernel

template <typename word>
UserInterface<word>::UserInterface(ContextPtr<word> context)
    : UserInterface(context, [&] {
#ifdef ENABLE_EXTENSION
        auto boot = std::dynamic_pointer_cast<const BootContext<word>>(context);
        if (boot && IsMEBootRoute(boot->boot_param_.route)) {
          return SparseSecretLayout::kSubringPrefix;
        }
#endif
        return SparseSecretLayout::kFullRing;
      }(), [&] {
#ifdef ENABLE_EXTENSION
        auto boot = std::dynamic_pointer_cast<const BootContext<word>>(context);
        if (boot) {
          return boot->boot_param_.extension;
        }
#endif
        return 16;
      }(), [&] {
#ifdef ENABLE_EXTENSION
        auto boot = std::dynamic_pointer_cast<const BootContext<word>>(context);
        if (boot && IsMEBootRoute(boot->boot_param_.route)) return kErrorStandardDeviation;
#endif
        return kSubringEntryStandardDeviation;
      }()) {}

template <typename word>
UserInterface<word>::UserInterface(ContextPtr<word> context,
                                  SparseSecretLayout sparse_layout)
    : UserInterface(std::move(context), sparse_layout, 16,
                    kSubringEntryStandardDeviation) {}

template <typename word>
UserInterface<word>::UserInterface(ContextPtr<word> context,
                                  SparseSecretLayout sparse_layout,
                                  int subring_extension, double entry_error_sigma)
    : context_{std::move(context)}, sparse_layout_{sparse_layout},
      subring_extension_{subring_extension}, entry_error_sigma_{entry_error_sigma} {
  const auto &param = context_->param_;
#ifdef ENABLE_EXTENSION
  auto boot = std::dynamic_pointer_cast<const BootContext<word>>(context_);
  AssertTrue(!boot || !IsMEBootRoute(boot->boot_param_.route) ||
                 sparse_layout_ == SparseSecretLayout::kSubringPrefix,
             "MEBOOT requires Subring Prefix client secrets");
#endif
  if (sparse_layout_ == SparseSecretLayout::kSubringPrefix) {
    AssertTrue(param.IsUsingSparseSecretEncapsulation(),
               "Subring Prefix requires sparse secret encapsulation");
    AssertTrue(subring_extension_ == 16 || subring_extension_ == 8,
               "Unsupported Subring Prefix extension");
    AssertTrue(std::isfinite(entry_error_sigma_) && entry_error_sigma_ >= 0,
               "Invalid entry error sigma");
    AssertTrue(param.GetSparseHammingWeight() <= param.degree_ / subring_extension_,
               "Subring Prefix sparse weight exceeds subring capacity");
    AssertTrue(param.max_level_ == param.max_key_switch_level_ ||
                   (subring_extension_ == 16 && param.GetSparseHammingWeight() == 256) ||
                   (subring_extension_ == 8 && param.GetSparseHammingWeight() == 40 &&
                    param.main_primes_[0] == 4398044938241ULL &&
                    param.entry_primes_ == std::vector<word>{786433} &&
                    entry_error_sigma_ == 3.2),
               "OverModRaise Subring Prefix requires the qualified sparse weight 256");
  }
  all_primes_ =
      param.GetPrimeVector(param.LevelToNP(param.max_level_, param.alpha_));

  if (!cm_populated_) {
    PopulateConstantMemory(context_->param_);
    cm_populated_ = true;
  }

  Warn(
      "UserInterface is for testing purposes only. "
      "Do not use in production.");
  PrepareSecrets();
  PrepareBasicEvks();
}

template <typename word>
void UserInterface<word>::Encrypt(Ct &ctxt, const Pt &ptxt) const {
  NPInfo np = ptxt.GetNP();
  AssertTrue(np.IsSubsetOf(context_->param_.GetKeySwitchNP(
                 context_->param_.max_key_switch_level_)),
             "Ordinary encryption is forbidden at OverModRaise levels");

  // Setting metadata
  ctxt.RemoveRx();
  ctxt.ModifyNP(np);
  ctxt.SetScale(ptxt.GetScale());
  ctxt.SetNumSlots(ptxt.GetNumSlots());

  SampleRandomPolynomial(ctxt.ax_, np);
  int num_q_primes = np.num_main_ + np.num_ter_;
  int num_total_primes = np.GetNumTotal();
  int num_aux = np.num_aux_;
  int degree = context_->param_.degree_;

  Dv ex_dv(num_total_primes * degree);
  SampleError(ex_dv, np);

  // Prepare PtrLists
  const word *primes = context_->param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = context_->param_.GetInvPrimesPtr(np);

  InputPtrList<word, 1> sx;
  sx.ptrs_[0] = main_secret_.data() +
                (context_->param_.GetMaxNumTer() - np.num_ter_) * degree;
  sx.extra_ = (context_->param_.GetMaxNumMain() - np.num_main_) * degree;
  InputPtrList<word, 1> mx(ptxt.ConstView());
  InputPtrList<word, 1> ex(ex_dv.ConstView(num_aux * degree));
  auto ctxt_temp = ctxt.ViewVector();
  OutputPtrList<word, 2> dst(ctxt_temp);

  int grid_dim = num_total_primes * degree / kernel_block_dim_;

  // bx = -ax * sx + mx + ex
  kernel::Encrypt<word><<<grid_dim, kernel_block_dim_>>>(
      dst, primes, inv_primes, num_q_primes, sx, mx, ex);
}

template <typename word>
void UserInterface<word>::EncryptUnderContract(Ct &ctxt, const Pt &ptxt,
                                               word c1_constant,
                                               bool use_sparse_secret) const {
  NPInfo q_np = ptxt.GetNP();
  AssertTrue(q_np.num_aux_ == 0,
             "EncryptUnderContract: Plaintext must not have auxiliary primes");
  int num_q = q_np.GetNumQ();
  int degree = context_->param_.degree_;
  int max_num_ter = context_->param_.GetMaxNumTer();
  int ter_left = max_num_ter - q_np.num_ter_;

  ctxt.RemoveRx();
  ctxt.ModifyNP(q_np);
  ctxt.SetScale(ptxt.GetScale());
  ctxt.SetNumSlots(ptxt.GetNumSlots());

  HostVector<word> h_c1(num_q * degree, 0);
  for (int i = 0; i < num_q; ++i) {
    h_c1[i * degree] = c1_constant;
  }
  CopyHostToDevice(ctxt.ax_, h_c1);
  auto ax_view = ctxt.AxView();
  context_->ntt_handler_.NTT(ax_view, q_np, ctxt.AxConstView(), true);

  DvConstView<word> s_view =
      (use_sparse_secret && context_->param_.IsUsingSparseSecretEncapsulation())
          ? SparseSecretConstView(ter_left)
          : MainSecretConstView(ter_left);

  Dv c1_s(num_q * degree);
  std::vector<DvView<word>> c1_s_view{c1_s.View(0)};
  context_->elem_handler_.Mult(c1_s_view, q_np, {ctxt.AxConstView()}, {s_view});

  std::vector<DvView<word>> bx_view{ctxt.BxView()};
  context_->elem_handler_.Sub(bx_view, q_np, {ptxt.ConstView()},
                              {c1_s.ConstView(0)});
}

template <typename word>
void UserInterface<word>::Decrypt(Pt &ptxt, const Ct &ctxt) const {
  NPInfo np = ctxt.GetNP();
  AssertTrue(np.num_aux_ == 0, "Decrypt: ModDown required before decryption");
  AssertTrue(!ctxt.HasRx(), "Decrypt: Rx should be removed before decryption");
  int degree = context_->param_.degree_;

  // Setting metadata
  ptxt.ModifyNP(np);
  ptxt.SetScale(ctxt.GetScale());
  ptxt.SetNumSlots(ctxt.GetNumSlots());
  int secret_front_ignore = context_->param_.GetMaxNumTer() - np.num_ter_;

  // ax * sx + bx ~= mx
  std::vector<DvView<word>> paccum_res{ptxt.View()};
  context_->elem_handler_.PAccum(
      paccum_res, np,
      {std::vector<DvConstView<word>>{ctxt.AxConstView()},
       std::vector<DvConstView<word>>{ctxt.BxConstView()}},
      {MainSecretConstView(secret_front_ignore)});
}

template <typename word>
void UserInterface<word>::DecryptSparse(Pt &ptxt, const Ct &ctxt) const {
  AssertTrue(context_->param_.IsUsingSparseSecretEncapsulation(),
             "DecryptSparse: Sparse secret encapsulation not enabled");
  NPInfo np = ctxt.GetNP();
  AssertTrue(np.num_aux_ == 0, "Decrypt: ModDown required before decryption");
  AssertTrue(!ctxt.HasRx(), "Decrypt: Rx should be removed before decryption");

  // Setting metadata
  ptxt.ModifyNP(np);
  ptxt.SetScale(ctxt.GetScale());
  ptxt.SetNumSlots(ctxt.GetNumSlots());
  int secret_front_ignore = context_->param_.GetMaxNumTer() - np.num_ter_;

  // ax * sx + bx ~= mx
  std::vector<DvView<word>> paccum_res{ptxt.View()};
  context_->elem_handler_.PAccum(
      paccum_res, np,
      {std::vector<DvConstView<word>>{ctxt.AxConstView()},
       std::vector<DvConstView<word>>{ctxt.BxConstView()}},
      {SparseSecretConstView(secret_front_ignore)});
}

template <typename word>
const EvaluationKey<word> &UserInterface<word>::GetRotationKey(
    int rot_idx) const {
  return evk_map_.GetRotationKey(rot_idx);
}

template <typename word>
const EvaluationKey<word> &UserInterface<word>::GetMultiplicationKey() const {
  return evk_map_.GetMultiplicationKey();
}

template <typename word>
const EvaluationKey<word> &UserInterface<word>::GetConjugationKey() const {
  return evk_map_.GetConjugationKey();
}

template <typename word>
const EvaluationKey<word> &UserInterface<word>::GetDenseToSparseKey() const {
  return evk_map_.GetDenseToSparseKey();
}

template <typename word>
const EvaluationKey<word> &UserInterface<word>::GetSparseToDenseKey() const {
  return evk_map_.GetSparseToDenseKey();
}

template <typename word>
const EvkMap<word> &UserInterface<word>::GetEvkMap() const {
  return evk_map_;
}

template <typename word>
const AKSKeyMap<word> &UserInterface<word>::GetAKSKeyMap() const {
  return evk_map_.GetAKSKeyMap();
}

template <typename word>
void UserInterface<word>::PrepareSecrets() {
  int degree = context_->param_.degree_;
  int num_total_primes = all_primes_.size();
  int alpha = context_->param_.alpha_;

  NPInfo np_max = context_->param_.LevelToNP(context_->param_.max_level_,
                                             context_->param_.alpha_);

  // Sampling sparse ternary secret
  int hamming_weight = context_->param_.GetDenseHammingWeight();
  std::vector<int> indices(hamming_weight);
  std::vector<word> ternary_values(hamming_weight);
  Random::SampleWithoutReplacement(indices.data(), hamming_weight, 0,
                                   degree - 1);
  Random::SampleUniformWord<word>(ternary_values.data(), hamming_weight, 0, 1);

  HostVector<word> main_s(num_total_primes * degree, 0);
  for (int i = 0; i < num_total_primes; i++) {
    word prime = all_primes_[i];
    for (int j = 0; j < hamming_weight; j++) {
      // 0 --> 1, 1 --> (prime - 1)
      main_s[i * degree + indices[j]] = (prime - 2) * ternary_values[j] + 1;
    }
  }
  CopyHostToDevice(main_secret_, main_s);
  auto main_sx_view = MainSecretView();
  context_->ntt_handler_.NTT(main_sx_view, np_max, MainSecretConstView(), true);

  if (!context_->param_.IsUsingSparseSecretEncapsulation()) {
    return;
  }

  // Sampling for sparse secret
  // We also prepare it for the maximum size
  hamming_weight = context_->param_.GetSparseHammingWeight();
  indices.resize(hamming_weight);
  ternary_values.resize(hamming_weight);
  const int stride =
      sparse_layout_ == SparseSecretLayout::kSubringPrefix ? subring_extension_ : 1;
  Random::SampleWithoutReplacement(indices.data(), hamming_weight, 0,
                                   (degree / stride) - 1);
  for (int j = 0; j < hamming_weight; j++) {
    indices[j] *= stride;
  }
  Random::SampleUniformWord<word>(ternary_values.data(), hamming_weight, 0, 1);
  HostVector<word> sparse_s(num_total_primes * degree, 0);
  for (int i = 0; i < num_total_primes; i++) {
    word prime = all_primes_[i];
    for (int j = 0; j < hamming_weight; j++) {
      // 0 --> 1, 1 --> (prime - 1)
      sparse_s[i * degree + indices[j]] = (prime - 2) * ternary_values[j] + 1;
    }
  }
  CopyHostToDevice(sparse_secret_, sparse_s);
  auto sparse_sx_view = SparseSecretView();
  context_->ntt_handler_.NTT(sparse_sx_view, np_max, SparseSecretConstView(),
                             true);
}

template <typename word>
void UserInterface<word>::PrepareRotationKey(int rot_idx, int max_level) {
  if (max_level == -1) max_level = context_->param_.max_key_switch_level_;
  int L = context_->param_.L_;
  int alpha = context_->param_.alpha_;
  int max_num_ter = context_->param_.GetMaxNumTer();
  int degree = context_->param_.degree_;
  NPInfo np = GetNPForEvk(max_level);
  int half_degree = degree / 2;

  AssertTrue(rot_idx > 0 && rot_idx < half_degree,
             "Invalid rotation index " + std::to_string(rot_idx));
  if (evk_map_.HasRotationKey(rot_idx)) {  // if already prepared
    const Evk &evk = evk_map_.GetRotationKey(rot_idx);
    if (np.IsSubsetOf(evk.GetNP())) {
      Warn("Rotation key for rotation index " + std::to_string(rot_idx) +
           " already prepared");
      return;
    }
  }

  Dv s_rot_dv(np.GetNumTotal() * degree);
  int aux_size = alpha * degree;
  std::vector<DvView<word>> s_rot{s_rot_dv.View(aux_size)};
  int ter_left = max_num_ter - np.num_ter_;

  // we permute it in the opposite direction
  context_->elem_handler_.Permute(s_rot, np, half_degree - rot_idx,
                                  {MainSecretConstView(ter_left)});
  int num_q = np.num_main_ + np.num_ter_;
  int beta = DivCeil(num_q, np.num_aux_);
  Evk &evk = evk_map_.EmplaceRotationKey(rot_idx, np, beta);
  PopulateEvk(evk, np, s_rot_dv, main_secret_);
}

template <typename word>
void UserInterface<word>::PrepareRotationKey(const EvkRequest &evk_request) {
  for (const auto &[rot_idx, level] : evk_request) {
    if (rot_idx == 0) continue;
    AssertTrue(rot_idx > 0,
               "Invalid rotation index " + std::to_string(rot_idx));
    PrepareRotationKey(Abs(rot_idx), level);
  }
}

template <typename word>
void UserInterface<word>::PrepareBasicEvks() {
  int L = context_->param_.L_;
  int alpha = context_->param_.alpha_;
  int degree = context_->param_.degree_;
  NPInfo np = GetNPForEvk(context_->param_.max_key_switch_level_);
  int num_q = np.num_main_ + np.num_ter_;
  int beta = DivCeil(num_q, np.num_aux_);

  // Multiplication key
  Dv s_squared(np.GetNumTotal() * degree);
  std::vector<DvView<word>> s_squared_view{s_squared.View(alpha * degree)};
  std::vector<DvConstView<word>> sx_view{
      MainSecretConstView(context_->param_.GetMaxNumTer() - np.num_ter_)};

  context_->elem_handler_.Mult(s_squared_view, np, sx_view, sx_view);
  Evk &mult_evk = evk_map_.EmplaceMultiplicationKey(np, beta);
  PopulateEvk(mult_evk, np, main_secret_, s_squared);

  // Conjugation key
  Dv s_conj(np.GetNumTotal() * degree);
  std::vector<DvView<word>> s_conj_view{s_conj.View(alpha * degree)};
  context_->elem_handler_.Permute(s_conj_view, np, -1, sx_view);
  Evk &conj_evk = evk_map_.EmplaceConjugationKey(np, beta);
  PopulateEvk(conj_evk, np, s_conj, main_secret_);

  if (context_->param_.IsUsingSparseSecretEncapsulation()) {
    // Dense to Sparse key
    NPInfo dts_np = GetNPForEvk(-1);
    int dts_num_q = dts_np.num_main_ + dts_np.num_ter_;
    int dts_beta = context_->entry_gadget_
                       ? context_->entry_gadget_->GetDigitCount()
                       : DivCeil(dts_num_q, dts_np.num_aux_);
    Evk &dts_evk = evk_map_.EmplaceDenseToSparseKey(dts_np, dts_beta);
    // Subring entry uses degree N/subring_extension_. Its noise budget is separate
    // from dense Qmax/P keys; do not inherit the dense-key security estimate.
    double entry_sigma = sparse_layout_ == SparseSecretLayout::kSubringPrefix
                             ? entry_error_sigma_
                             : kErrorStandardDeviation;
    if (context_->entry_gadget_) {
      AssertTrue(sparse_layout_ == SparseSecretLayout::kSubringPrefix,
                 "Low-q0P experiment requires Subring Prefix secrets");
      int offset = context_->param_.GetMaxNumTer() * degree;
      context_->entry_gadget_->PopulateKey(
          dts_evk, DvConstView<word>(main_secret_.data() + offset, degree),
          DvConstView<word>(sparse_secret_.data() + offset, degree), entry_sigma);
    } else {
      PopulateEvk(dts_evk, dts_np, sparse_secret_, main_secret_, entry_sigma);
    }

    // Both bootstrap routes require every sparse-to-dense decomposition digit.
    Evk &std_key = evk_map_.EmplaceSparseToDenseKey(np, beta);
    PopulateEvk(std_key, np, main_secret_, sparse_secret_);
#ifdef ENABLE_EXTENSION
    if (std::dynamic_pointer_cast<const BootContext<word>>(context_)) return;
#endif
    // Retain the core context's sparse-to-dense key simplification.
    // TODO: this key has beta times higher error than necessary.
    std::vector<DvView<word>> std_key_view = std_key.ViewVector(0);
    std::vector<std::vector<DvConstView<word>>> std_key_accum_inputs;
    for (int i = 0; i < beta; i++) {
      std_key_accum_inputs.push_back(std_key.ConstViewVector(i));
    }
    context_->elem_handler_.Accum(std_key_view, np, std_key_accum_inputs);
  }
}

// encryption_secret and target_secret are always prepared at the maximum level
// Or exactly follows the np
template <typename word>
void UserInterface<word>::PopulateEvk(Evk &evk, const NPInfo &np,
                                      const Dv &encryption_secret,
                                      const Dv &target_secret,
                                      double error_sigma) {
  int degree = context_->param_.degree_;
  // Beware that alpha != np.num_aux_
  int alpha = context_->param_.alpha_;
  int L = context_->param_.L_;
  int max_num_ter = context_->param_.GetMaxNumTer();
  int num_q = np.num_main_ + np.num_ter_;
  int beta = DivCeil(num_q, np.num_aux_);

  int ter_left = max_num_ter - np.num_ter_;
  int main_left = context_->param_.GetMaxNumMain() - np.num_main_;

  // Preparing input pointers
  int enc_s_size = encryption_secret.size();
  InputPtrList<word, 1> enc_s;
  if (enc_s_size == (L + alpha) * degree) {
    enc_s.ptrs_[0] = encryption_secret.data() + ter_left * degree;
    enc_s.extra_ = main_left * degree;
  } else if (enc_s_size == np.GetNumTotal() * degree) {
    enc_s.ptrs_[0] = encryption_secret.data();
    enc_s.extra_ = 0;
  } else {
    Fail("PopulateEvk: encryption secret is not sized correctly");
  }
  int target_s_size = target_secret.size();
  const word *target_s_ptr = nullptr;
  if (target_s_size == (L + alpha) * degree) {
    target_s_ptr = target_secret.data() + ter_left * degree;
  } else if (target_s_size == np.GetNumTotal() * degree) {
    target_s_ptr = target_secret.data();
  } else {
    Fail("PopulateEvk: target secret is not sized correctly");
  }

  Dv p_prod(num_q);
  // Prepare p_prod
  HostVector<word> h_p_prod(num_q);
  for (int i = 0; i < num_q; i++) {
    word mod_prime = all_primes_[i + ter_left];
    word p_prod_mod_qi = 1;
    for (int j = 0; j < np.num_aux_; j++) {
      p_prod_mod_qi =
          primeutil::MultMod(p_prod_mod_qi, all_primes_[j + L], mod_prime);
    }
    h_p_prod[i] = primeutil::ToMontgomery(p_prod_mod_qi, mod_prime);
  }
  CopyHostToDevice(p_prod, h_p_prod);

  // 1. Prepare primes
  const word *primes = context_->param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = context_->param_.GetInvPrimesPtr(np);

  // Prepare each of beta pairs.
  // bx = -ax * encryption_secret + target_secret * const + error
  for (int i = 0; i < beta; i++) {
    // Preparing the encryption of 0
    SampleRandomPolynomial(evk.ax_.at(i), np);
    Dv ex_dv(np.GetNumTotal() * degree);
    SampleError(ex_dv, np, error_sigma);

    auto evk_temp = evk.ViewVector(i);
    OutputPtrList<word, 2> evk_ptr_list(evk_temp);
    InputPtrList<word, 1> ex(ex_dv.ConstView(np.num_aux_ * degree));

    int grid_dim = (num_q + np.num_aux_) * degree / kernel_block_dim_;

    kernel::EncryptZero<word><<<grid_dim, kernel_block_dim_>>>(
        evk_ptr_list, primes, inv_primes, num_q, enc_s, ex);

    int chunk_size = np.num_aux_;
    if (i == beta - 1) {
      chunk_size = num_q - (beta - 1) * np.num_aux_;
    }
    grid_dim = chunk_size * degree / kernel_block_dim_;

    kernel::AddEvkPart<word><<<grid_dim, kernel_block_dim_>>>(
        evk.bx_.at(i).data() + i * np.num_aux_ * degree,
        primes + i * np.num_aux_, inv_primes + i * np.num_aux_,
        target_s_ptr + i * np.num_aux_ * degree,
        p_prod.data() + i * np.num_aux_);
  }
}

// We can regard this random polynomial as having any form we want
// It can be regarded to be NTT-applied or not (with or without Montgomery form)
template <typename word>
void UserInterface<word>::SampleRandomPolynomial(Dv &poly,
                                                 const NPInfo &np) const {
  int degree = context_->param_.degree_;
  int max_num_ter = context_->param_.GetMaxNumTer();
  int num_q = np.num_main_ + np.num_ter_;
  int L = context_->param_.L_;
  int num_total_primes = num_q + np.num_aux_;
  AssertTrue(num_total_primes * degree == static_cast<int>(poly.size()),
             "SampleRandomPolynomial: Invalid poly size");
  int prime_offset = max_num_ter - np.num_ter_;

  HostVector<word> poly_host(num_total_primes * degree, 0);
  for (int i = 0; i < num_total_primes; i++) {
    int prime_index = i + prime_offset;
    if (i >= num_q) {
      prime_index = L + i - num_q;
    }
    word prime = all_primes_[prime_index];
    Random::SampleUniformWord<word>(poly_host.data() + i * degree, degree, 0,
                                    prime - 1);
  }
  CopyHostToDevice(poly, poly_host);
}

template <typename word>
void UserInterface<word>::SampleError(Dv &poly, const NPInfo &np,
                                     double error_sigma) const {
  int degree = context_->param_.degree_;
  int param_max_level = context_->param_.max_level_;
  int max_num_ter = context_->param_.GetMaxNumTer();
  int num_q = np.num_main_ + np.num_ter_;
  int L = context_->param_.L_;
  int num_total_primes = num_q + np.num_aux_;
  AssertTrue(num_total_primes * degree == static_cast<int>(poly.size()),
             "SampleError: Invalid poly size");
  int prime_offset = max_num_ter - np.num_ter_;

  HostVector<word> poly_host(num_total_primes * degree, 0);

  std::vector<int> error(degree);
  Random::SampleDiscreteNormal(error.data(), degree, 0, error_sigma);
  for (int i = 0; i < num_total_primes; i++) {
    int prime_index = i + prime_offset;
    if (i >= num_q) {
      prime_index = L + i - num_q;
    }
    word prime = all_primes_[prime_index];
    for (int j = 0; j < degree; j++) {
      poly_host[i * degree + j] =
          ((error[j] < 0) ? prime - static_cast<word>(-error[j])
                          : static_cast<word>(error[j]));
    }
  }
  CopyHostToDevice(poly, poly_host);
  int aux_size = np.num_aux_ * degree;
  auto poly_view = poly.View(aux_size);
  context_->ntt_handler_.NTT(poly_view, np, poly.ConstView(aux_size), true);
}

template <typename word>
NPInfo UserInterface<word>::GetNPForEvk(int max_level) const {
  // DtS case
  if (max_level == -1) {
    NPInfo short_base = context_->param_.LevelToNP(-1);
    short_base.num_aux_ = short_base.num_main_ + short_base.num_ter_;
    return short_base;
  }

  // Normal case
  return context_->param_.GetKeySwitchNP(max_level);
}

template <typename word>
DvView<word> UserInterface<word>::MainSecretView(int front_ignore /*= 0*/) {
  int degree = context_->param_.degree_;
  int alpha = context_->param_.alpha_;
  return main_secret_.View(alpha * degree, front_ignore * degree);
}

template <typename word>
DvConstView<word> UserInterface<word>::MainSecretConstView(
    int front_ignore /*= 0*/) const {
  int degree = context_->param_.degree_;
  int alpha = context_->param_.alpha_;
  return main_secret_.ConstView(alpha * degree, front_ignore * degree);
}

template <typename word>
DvView<word> UserInterface<word>::SparseSecretView(int front_ignore /*= 0*/) {
  AssertTrue(context_->param_.IsUsingSparseSecretEncapsulation(),
             "Sparse secret is not used");
  int degree = context_->param_.degree_;
  int alpha = context_->param_.alpha_;
  return sparse_secret_.View(alpha * degree, front_ignore * degree);
}

template <typename word>
DvConstView<word> UserInterface<word>::SparseSecretConstView(
    int front_ignore /*= 0*/) const {
  AssertTrue(context_->param_.IsUsingSparseSecretEncapsulation(),
             "Sparse secret is not used");
  int degree = context_->param_.degree_;
  int alpha = context_->param_.alpha_;
  return sparse_secret_.ConstView(alpha * degree, front_ignore * degree);
}

namespace {

template <typename word>
__global__ void AKSProjectKernel(
    word *dst_z,
    const word *src_y,
    const word *qp_primes,
    const word *P_mod_qi,
    const word *inv_qL_mod_qp,
    word R_P,
    word Q_P_mod_qL,
    word inv_q0_mod_q1,
    word inv_q0q1_mod_q2,
    int num_q,
    int alpha,
    int degree) {
  int d = blockIdx.x * blockDim.x + threadIdx.x;
  if (d >= degree) return;

  int L = num_q - 1;
  word q_L = qp_primes[L];

  word y_0 = src_y[0 * degree + d];
  word y_1 = (num_q > 1) ? src_y[1 * degree + d] : 0;
  word y_2 = (num_q > 2) ? src_y[2 * degree + d] : 0;

  bool is_y_neg = false;
  uint128_t abs_y = 0;
  basic::ReconstructCenteredCRT<word>(
      is_y_neg, abs_y, y_0, y_1, y_2, qp_primes, inv_q0_mod_q1,
      inv_q0q1_mod_q2, num_q);

  word R_y = src_y[L * degree + d];
  word Q_y_mod_qL;
  if (!is_y_neg) {
    uint128_t q_y = (abs_y - static_cast<uint128_t>(R_y)) /
                    static_cast<uint128_t>(q_L);
    Q_y_mod_qL = static_cast<word>(q_y % static_cast<uint128_t>(q_L));
  } else {
    uint128_t abs_q_y = (abs_y + static_cast<uint128_t>(R_y)) /
                        static_cast<uint128_t>(q_L);
    word rem_q_y = static_cast<word>(abs_q_y % static_cast<uint128_t>(q_L));
    Q_y_mod_qL = basic::Negate<word>(rem_q_y, q_L);
  }

  auto prod = basic::detail::__mult_wide<word>(R_y, R_P);
  word rem = static_cast<word>(prod % q_L);
  word quot = static_cast<word>(prod / q_L);
  bool is_c_neg = (rem > (q_L >> 1));
  word abs_c = is_c_neg ? (q_L - rem) : rem;
  word carry = is_c_neg ? basic::Add<word>(quot, 1, q_L) : quot;

  for (int i = 0; i < L; ++i) {
    word q_i = qp_primes[i];
    word y_i = src_y[i * degree + d];
    word yp_i = static_cast<word>(
        basic::detail::__mult_wide<word>(y_i, P_mod_qi[i]) % q_i);
    word c_mod_qi = static_cast<word>(abs_c % q_i);
    word num_i = is_c_neg ? basic::Add<word>(yp_i, c_mod_qi, q_i)
                          : basic::Sub<word>(yp_i, c_mod_qi, q_i);
    dst_z[i * degree + d] = static_cast<word>(
        basic::detail::__mult_wide<word>(num_i, inv_qL_mod_qp[i]) % q_i);
  }

  word term1 = static_cast<word>(
      basic::detail::__mult_wide<word>(Q_y_mod_qL, R_P) % q_L);
  word term2 = static_cast<word>(
      basic::detail::__mult_wide<word>(R_y, Q_P_mod_qL) % q_L);
  word z_L = basic::Add<word>(term1, term2, q_L);
  z_L = basic::Add<word>(z_L, carry, q_L);
  dst_z[L * degree + d] = z_L;

  for (int k = 0; k < alpha; ++k) {
    int idx = num_q + k;
    word p_k = qp_primes[idx];
    word c_mod_pk = static_cast<word>(abs_c % p_k);
    word neg_c_mod_pk = is_c_neg ? c_mod_pk : basic::Negate<word>(c_mod_pk, p_k);
    dst_z[idx * degree + d] = static_cast<word>(
        basic::detail::__mult_wide<word>(neg_c_mod_pk, inv_qL_mod_qp[idx]) % p_k);
  }
}

}  // namespace

template <typename word>
void UserInterface<word>::PrepareAKSKeys(
    const std::vector<int> &rot_indices,
    const std::map<int, Plaintext<word>> &diagonals,
    int level /*= -1*/,
    bool from_sparse /*= false*/) {
  if (level == -1) {
    level = context_->param_.max_key_switch_level_;
  }
  AssertTrue(level <= context_->param_.max_key_switch_level_,
             "AKS keys are forbidden at OverModRaise levels");
  int degree = context_->param_.degree_;
  int half_degree = degree / 2;
  int alpha = context_->param_.alpha_;
  int max_num_ter = context_->param_.GetMaxNumTer();

  NPInfo np = context_->param_.LevelToNP(level, context_->param_.alpha_);
  int num_q = np.num_main_ + np.num_ter_;
  int beta = 1;

  int ter_left = max_num_ter - np.num_ter_;
  int main_left = context_->param_.GetMaxNumMain() - np.num_main_;

  // Primes info for level
  NPInfo q_np(np.num_main_, np.num_ter_, 0);
  std::vector<word> q_primes(all_primes_.begin() + ter_left,
                             all_primes_.begin() + ter_left + num_q);
  std::vector<word> p_primes(all_primes_.begin() + context_->param_.L_,
                             all_primes_.begin() + context_->param_.L_ + alpha);

  // Precompute P constants for GPU AKS projection: P = q_L * Q_P + R_P
  BigInt big_P(static_cast<uint64_t>(1));
  for (int i = 0; i < alpha; i++) {
    BigInt big_pi(static_cast<uint64_t>(p_primes[i]));
    BigInt::Mult(big_P, big_P, big_pi);
  }

  word q_L_val = q_primes.back();
  BigInt big_q_L(static_cast<uint64_t>(q_L_val));
  BigInt big_R_P(static_cast<uint64_t>(0));
  BigInt::Mod(big_R_P, big_P, big_q_L);
  word R_P = static_cast<word>(big_R_P.GetUnsigned());

  BigInt big_Q_P(static_cast<uint64_t>(0));
  BigInt::Sub(big_Q_P, big_P, big_R_P);
  BigInt::Div(big_Q_P, big_Q_P, big_q_L);
  BigInt::Mod(big_Q_P, big_Q_P, big_q_L);
  word Q_P_mod_qL = static_cast<word>(big_Q_P.GetUnsigned());

  HostVector<word> h_qp_primes(num_q + alpha);
  HostVector<word> h_P_mod_qi(num_q);
  HostVector<word> h_inv_qL_mod_qp(num_q + alpha, 0);
  for (int i = 0; i < num_q; i++) {
    word q_i = q_primes[i];
    h_qp_primes[i] = q_i;
    BigInt big_qi(static_cast<uint64_t>(q_i));
    BigInt p_mod_qi(static_cast<uint64_t>(0));
    BigInt::Mod(p_mod_qi, big_P, big_qi);
    h_P_mod_qi[i] = static_cast<word>(p_mod_qi.GetUnsigned());
    if (i < num_q - 1) {
      h_inv_qL_mod_qp[i] =
          primeutil::InvMod<word>(static_cast<word>(q_L_val % q_i), q_i);
    }
  }
  for (int k = 0; k < alpha; k++) {
    word p_k = p_primes[k];
    h_qp_primes[num_q + k] = p_k;
    h_inv_qL_mod_qp[num_q + k] =
        primeutil::InvMod<word>(static_cast<word>(q_L_val % p_k), p_k);
  }

  Dv qp_primes_gpu(num_q + alpha);
  Dv P_mod_qi_gpu(num_q);
  Dv inv_qL_mod_qp_gpu(num_q + alpha);
  CopyHostToDevice(qp_primes_gpu, h_qp_primes);
  CopyHostToDevice(P_mod_qi_gpu, h_P_mod_qi);
  CopyHostToDevice(inv_qL_mod_qp_gpu, h_inv_qL_mod_qp);

  word inv_q0_mod_q1 = 0;
  word inv_q0q1_mod_q2 = 0;
  if (num_q >= 2) {
    inv_q0_mod_q1 = primeutil::InvMod<word>(
        static_cast<word>(q_primes[0] % q_primes[1]), q_primes[1]);
  }
  if (num_q >= 3) {
    word q0q1_mod_q2 = primeutil::MultMod<word>(
        static_cast<word>(q_primes[0] % q_primes[2]),
        static_cast<word>(q_primes[1] % q_primes[2]), q_primes[2]);
    inv_q0q1_mod_q2 = primeutil::InvMod<word>(q0q1_mod_q2, q_primes[2]);
  }

  const Dv &s_in_dv =
      (from_sparse && context_->param_.IsUsingSparseSecretEncapsulation())
          ? sparse_secret_
          : main_secret_;

  // Prime pointers on GPU
  const word *primes_gpu = context_->param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes_gpu =
      context_->param_.GetInvPrimesPtr(np);

  for (int rot : rot_indices) {
    int rot_offset = rot % half_degree;
    if (rot_offset < 0) rot_offset += half_degree;

    auto it_diag = diagonals.find(rot);
    if (it_diag == diagonals.end()) {
      it_diag = diagonals.find(rot_offset);
    }
    AssertTrue(it_diag != diagonals.end(),
               "PrepareAKSKeys: Missing diagonal plaintext for rotation " +
                   std::to_string(rot));
    const Plaintext<word> &diag_pt = it_diag->second;

    // 1. Target secret permuted: s_dense_inv = tau_{-rot}(s_dense)
    Dv s_dense_inv(np.GetNumTotal() * degree);
    std::vector<DvView<word>> s_dense_inv_view{
        s_dense_inv.View(alpha * degree)};
    if (rot_offset == 0) {
      cudaMemcpy(s_dense_inv.data(), MainSecretConstView(ter_left).data(),
                 num_q * degree * sizeof(word),
                 cudaMemcpyDeviceToDevice);
      cudaMemcpy(s_dense_inv.data() + num_q * degree,
                 MainSecretConstView(ter_left).data() + (num_q + main_left) * degree,
                 alpha * degree * sizeof(word),
                 cudaMemcpyDeviceToDevice);
    } else {
      context_->elem_handler_.Permute(s_dense_inv_view, np,
                                      half_degree - rot_offset,
                                      {MainSecretConstView(ter_left)});
    }

    // 2. Diagonal permuted: m_k_inv = tau_{-rot}(m_k) in R_Q
    Dv m_k_inv(num_q * degree);
    std::vector<DvView<word>> m_k_inv_view{m_k_inv.View(0)};
    if (rot_offset == 0) {
      cudaMemcpy(m_k_inv.data(), diag_pt.ConstView(0).data(),
                 num_q * degree * sizeof(word), cudaMemcpyDeviceToDevice);
    } else {
      context_->elem_handler_.Permute(m_k_inv_view, q_np,
                                      half_degree - rot_offset,
                                      {diag_pt.ConstView(0)});
    }

    // 3. Y = s_in * m_k_inv in R_Q (NTT Montgomery domain)
    Dv Y_dv(num_q * degree);
    std::vector<DvView<word>> Y_view{Y_dv.View(0)};
    DvConstView<word> s_in_view =
        (s_in_dv.data() == main_secret_.data())
            ? MainSecretConstView(ter_left)
            : SparseSecretConstView(ter_left);
    context_->elem_handler_.Mult(Y_view, q_np, {s_in_view},
                                 {m_k_inv.ConstView(0)});

    // 4. INTT(Y) to coeff domain on GPU
    auto Y_dv_view = Y_dv.View(0);
    context_->ntt_handler_.INTT(Y_dv_view, q_np, Y_dv.ConstView(0));

    // 5. GPU centered division & projection: Z = round((Y * P) / q_L) in R_{QP}
    Dv Z_dv((num_q + alpha) * degree);
    int proj_block_dim = 256;
    int proj_grid_dim = degree / proj_block_dim;
    AKSProjectKernel<word><<<proj_grid_dim, proj_block_dim>>>(
        Z_dv.data(), Y_dv.data(), qp_primes_gpu.data(), P_mod_qi_gpu.data(),
        inv_qL_mod_qp_gpu.data(), R_P, Q_P_mod_qL, inv_q0_mod_q1,
        inv_q0q1_mod_q2, num_q, alpha, degree);

    // 6. Apply NTT (Montgomery form) on GPU
    int aux_size = alpha * degree;
    auto Z_dv_view = Z_dv.View(aux_size);
    context_->ntt_handler_.NTT(Z_dv_view, np, Z_dv.ConstView(aux_size), true);

    // 7. Construct EvaluationKey on device
    Evk &evk = evk_map_.EmplaceAKSKey(rot, np, beta);

    for (int i = 0; i < beta; i++) {
      // Sample ax and error e
      SampleRandomPolynomial(evk.ax_.at(i), np);
      Dv ex_dv(np.GetNumTotal() * degree);
      SampleError(ex_dv, np);

      auto evk_temp = evk.ViewVector(i);
      OutputPtrList<word, 2> evk_ptr_list(evk_temp);
      InputPtrList<word, 1> ex(ex_dv.ConstView(np.num_aux_ * degree));
      InputPtrList<word, 1> enc_s;
      enc_s.ptrs_[0] = s_dense_inv.data();
      enc_s.extra_ = 0;

      int grid_dim = (num_q + np.num_aux_) * degree / kernel_block_dim_;
      kernel::EncryptZero<word><<<grid_dim, kernel_block_dim_>>>(
          evk_ptr_list, primes_gpu, inv_primes_gpu, num_q, enc_s, ex);

      // Add Z to evk.bx_[0]
      if (i == 0) {
        std::vector<DvView<word>> bx_view{evk.BxView(0)};
        context_->elem_handler_.Add(bx_view, np, {evk.BxConstView(0)},
                                    {Z_dv.ConstView(aux_size)});
      }
    }
  }
}

#ifdef ENABLE_EXTENSION
template <typename word>
void UserInterface<word>::PrepareAKSKeys(const StripedMatrix &matrix, int level,
                                         double scale,
                                         bool from_sparse /*= false*/) {
  NPInfo np = context_->param_.LevelToNP(level, 0);
  std::vector<int> rot_indices;
  std::map<int, Plaintext<word>> encoded_diags;
  for (const auto &[rot, diag] : matrix) {
    rot_indices.push_back(rot);
    encoded_diags.try_emplace(rot, np);
    context_->encoder_.Encode(encoded_diags.at(rot), level, scale, diag);
  }
  PrepareAKSKeys(rot_indices, encoded_diags, level, from_sparse);
}

template <typename word>
void UserInterface<word>::PrepareMEBootAKSKeys(
    const MEBootCtS<word> &meboot_cts, bool from_sparse /*= true*/) {
  AssertTrue(sparse_layout_ == SparseSecretLayout::kSubringPrefix,
             "MEBootCtS requires Subring Prefix client secrets");
  PrepareAKSKeys(meboot_cts.GetStage2Matrix(), meboot_cts.GetStage2Level(),
                 meboot_cts.GetStage2LCRScale(), from_sparse);
  meboot_cts.BindAKSKeys(evk_map_.GetAKSKeyMap());
}
#endif

template class UserInterface<uint32_t>;
template class UserInterface<uint64_t>;

}  // namespace cheddar
