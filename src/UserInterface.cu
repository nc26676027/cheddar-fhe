#include "UserInterface.h"
#include "common/Basic.cuh"
#include "common/CommonUtils.h"
#include "common/ConstantMemory.cuh"
#include "common/PrimeUtils.h"
#include "common/PtrList.h"
#include "core/BigInt.h"

#ifdef ENABLE_EXTENSION
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
    : context_{std::move(context)} {
  const auto &param = context_->param_;
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
  return aks_key_map_;
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
  Random::SampleWithoutReplacement(indices.data(), hamming_weight, 0,
                                   degree - 1);
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
  int L = context_->param_.L_;
  int alpha = context_->param_.alpha_;
  int max_num_ter = context_->param_.GetMaxNumTer();
  int degree = context_->param_.degree_;
  NPInfo np = GetNPForEvk(max_level);
  int half_degree = degree / 2;

  AssertTrue(rot_idx > 0 && rot_idx < half_degree,
             "Invalid rotation index " + std::to_string(rot_idx));
  if (evk_map_.find(rot_idx) != evk_map_.end()) {  // if already prepared
    const Evk &evk = evk_map_.at(rot_idx);
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
  PrepareEvk(rot_idx, np, s_rot_dv, main_secret_);
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
  NPInfo np = GetNPForEvk(context_->param_.max_level_);

  // Multiplication key
  Dv s_squared(np.GetNumTotal() * degree);
  std::vector<DvView<word>> s_squared_view{s_squared.View(alpha * degree)};
  std::vector<DvConstView<word>> sx_view{MainSecretConstView()};

  context_->elem_handler_.Mult(s_squared_view, np, sx_view, sx_view);
  PrepareEvk(EvkMap<word>::kMultiplicationKeyIndex, np, main_secret_,
             s_squared);

  // Conjugation key
  Dv s_conj(np.GetNumTotal() * degree);
  std::vector<DvView<word>> s_conj_view{s_conj.View(alpha * degree)};
  context_->elem_handler_.Permute(s_conj_view, np, -1, sx_view);
  PrepareEvk(EvkMap<word>::kConjugationKeyIndex, np, s_conj, main_secret_);

  if (context_->param_.IsUsingSparseSecretEncapsulation()) {
    // Dense to Sparse key
    NPInfo dts_np = GetNPForEvk(-1);

    PrepareEvk(EvkMap<word>::kDenseToSparseKeyIndex, dts_np, sparse_secret_,
               main_secret_);

    // Sparse to Dense key
    PrepareEvk(EvkMap<word>::kSparseToDenseKeyIndex, np, main_secret_,
               sparse_secret_);
    // We can simplify SparseToDenseKey
    // TODO: this key has beta times higher error than necessary
    auto &std_key = evk_map_.at(EvkMap<word>::kSparseToDenseKeyIndex);
    int beta = DivCeil(L, alpha);
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
void UserInterface<word>::PrepareEvk(int key_idx, const NPInfo &np,
                                     const Dv &encryption_secret,
                                     const Dv &target_secret) {
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
    Fail("PrepareEvk: encryption secret is not sized correctly");
  }
  int target_s_size = target_secret.size();
  const word *target_s_ptr = nullptr;
  if (target_s_size == (L + alpha) * degree) {
    target_s_ptr = target_secret.data() + ter_left * degree;
  } else if (target_s_size == np.GetNumTotal() * degree) {
    target_s_ptr = target_secret.data();
  } else {
    Fail("PrepareEvk: target secret is not sized correctly");
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

  // Initialize object in the EvkMap
  if (evk_map_.find(key_idx) != evk_map_.end()) {
    Warn("Overwriting the evk for key index " + std::to_string(key_idx));
    evk_map_.erase(key_idx);
  }
  evk_map_.try_emplace(key_idx, np, beta);
  Evk &evk = evk_map_.at(key_idx);

  // 1. Prepare primes
  const word *primes = context_->param_.GetPrimesPtr(np);
  const make_signed_t<word> *inv_primes = context_->param_.GetInvPrimesPtr(np);

  // Prepare each of beta pairs.
  // bx = -ax * encryption_secret + target_secret * const + error
  for (int i = 0; i < beta; i++) {
    // Preparing the encryption of 0
    SampleRandomPolynomial(evk.ax_.at(i), np);
    Dv ex_dv(np.GetNumTotal() * degree);
    SampleError(ex_dv, np);

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
void UserInterface<word>::SampleError(Dv &poly, const NPInfo &np) const {
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
  Random::SampleDiscreteNormal(error.data(), degree, 0,
                               kErrorStandardDeviation);
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
  NPInfo res = context_->param_.LevelToNP(0);
  for (int i = 1; i <= max_level; i++) {
    NPInfo np_i = context_->param_.LevelToNP(i);
    res.num_main_ = Max(res.num_main_, np_i.num_main_);
  }
  res.num_ter_ = context_->param_.GetMaxNumTer();
  res.num_aux_ = context_->param_.alpha_;

  return res;
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

template <typename word>
void UserInterface<word>::PrepareAKSKeys(
    const std::vector<int> &rot_indices,
    const std::map<int, Plaintext<word>> &diagonals,
    int level /*= -1*/,
    bool from_sparse /*= false*/) {
  if (level == -1) {
    level = context_->param_.max_level_;
  }
  int degree = context_->param_.degree_;
  int half_degree = degree / 2;
  int alpha = context_->param_.alpha_;
  int max_num_ter = context_->param_.GetMaxNumTer();

  NPInfo np = context_->param_.LevelToNP(level, context_->param_.alpha_);
  int num_q = np.num_main_ + np.num_ter_;
  int beta = DivCeil(num_q, np.num_aux_);

  int ter_left = max_num_ter - np.num_ter_;
  int main_left = context_->param_.GetMaxNumMain() - np.num_main_;

  // Primes info for level
  NPInfo q_np(np.num_main_, np.num_ter_, 0);
  std::vector<word> q_primes(all_primes_.begin() + ter_left,
                             all_primes_.begin() + ter_left + num_q);
  std::vector<word> p_primes(all_primes_.begin() + context_->param_.L_,
                             all_primes_.begin() + context_->param_.L_ + alpha);

  // BigInt CRT constants for Q
  std::vector<BigInt> big_q_primes;
  big_q_primes.reserve(num_q);
  for (int i = 0; i < num_q; i++) {
    big_q_primes.emplace_back(static_cast<uint64_t>(q_primes[i]));
  }
  BigInt big_Q(static_cast<uint64_t>(1));
  for (int i = 0; i < num_q; i++) {
    BigInt::Mult(big_Q, big_Q, big_q_primes[i]);
  }
  BigInt half_big_Q(static_cast<uint64_t>(0));
  BigInt::Div2(half_big_Q, big_Q);

  // BigInt P
  BigInt big_P(static_cast<uint64_t>(1));
  for (int i = 0; i < alpha; i++) {
    BigInt big_pi(static_cast<uint64_t>(p_primes[i]));
    BigInt::Mult(big_P, big_P, big_pi);
  }

  // Last prime q_L
  word q_L_val = q_primes.back();
  BigInt big_q_L(static_cast<uint64_t>(q_L_val));
  BigInt half_big_q_L(static_cast<uint64_t>(0));
  BigInt::Div2(half_big_q_L, big_q_L);

  // Precompute CRT Reconstruction constants for Q:
  // q_star_i = Q / q_i
  // q_star_inv_i = q_star_i^-1 mod q_i
  // mult_factor_i = q_star_i * q_star_inv_i
  std::vector<BigInt> q_crt_factors;
  q_crt_factors.reserve(num_q);
  for (int i = 0; i < num_q; i++) {
    BigInt q_star(static_cast<uint64_t>(1));
    for (int j = 0; j < num_q; j++) {
      if (i != j) {
        BigInt::Mult(q_star, q_star, big_q_primes[j]);
      }
    }
    BigInt q_star_mod_qi(static_cast<uint64_t>(0));
    BigInt::Mod(q_star_mod_qi, q_star, big_q_primes[i]);
    word q_star_val = static_cast<word>(q_star_mod_qi.GetUnsigned());
    word inv_val = primeutil::InvMod<word>(q_star_val, q_primes[i]);
    BigInt big_inv(static_cast<uint64_t>(inv_val));
    BigInt factor(static_cast<uint64_t>(0));
    BigInt::Mult(factor, q_star, big_inv);
    q_crt_factors.push_back(factor);
  }

  // Precompute BigInt representations of P primes and Q primes
  std::vector<BigInt> big_all_primes;
  big_all_primes.reserve(num_q + alpha);
  for (int i = 0; i < num_q; i++) {
    big_all_primes.emplace_back(static_cast<uint64_t>(q_primes[i]));
  }
  for (int i = 0; i < alpha; i++) {
    big_all_primes.emplace_back(static_cast<uint64_t>(p_primes[i]));
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

    // 4. INTT(Y) to coeff domain
    auto Y_dv_view = Y_dv.View(0);
    context_->ntt_handler_.INTT(Y_dv_view, q_np, Y_dv.ConstView(0));

    // 5. Transfer INTT result to host for exact integer centered division
    HostVector<word> h_Y(num_q * degree);
    CopyDeviceToHost(h_Y, Y_dv);

    // 6. Host CRT reconstruction and division:
    // Z = round( (Y * P) / q_L )
    HostVector<word> h_Z((num_q + alpha) * degree);
    BigInt y_big(static_cast<uint64_t>(0));
    BigInt residue(static_cast<uint64_t>(0));
    BigInt term(static_cast<uint64_t>(0));
    BigInt y_times_P(static_cast<uint64_t>(0));
    BigInt rem(static_cast<uint64_t>(0));
    BigInt z_big(static_cast<uint64_t>(0));
    BigInt mod_val(static_cast<uint64_t>(0));

    for (int d = 0; d < degree; d++) {
      y_big.Set(0);
      for (int i = 0; i < num_q; i++) {
        residue.Set(static_cast<uint64_t>(h_Y[i * degree + d]));
        BigInt::Mult(term, residue, q_crt_factors[i]);
        BigInt::Add(y_big, y_big, term);
      }
      BigInt::NormalizeMod(y_big, y_big, big_Q, half_big_Q);

      // Multiply by P
      BigInt::Mult(y_times_P, y_big, big_P);

      // Centered modulo q_L
      BigInt::Mod(rem, y_times_P, big_q_L);
      if (rem.GetDouble() > half_big_q_L.GetDouble()) {
        BigInt::Sub(rem, rem, big_q_L);
      }
      BigInt::Sub(z_big, y_times_P, rem);
      BigInt::Div(z_big, z_big, big_q_L);

      // Reduce z_big modulo all (Q + P) primes
      for (int i = 0; i < num_q + alpha; i++) {
        BigInt::Mod(mod_val, z_big, big_all_primes[i]);
        if (mod_val.GetDouble() < 0) {
          BigInt::Add(mod_val, mod_val, big_all_primes[i]);
        }
        h_Z[i * degree + d] = static_cast<word>(mod_val.GetUnsigned());
      }
    }

    // 7. Copy Z to device and apply NTT (Montgomery form)
    Dv Z_dv((num_q + alpha) * degree);
    CopyHostToDevice(Z_dv, h_Z);
    int aux_size = alpha * degree;
    auto Z_dv_view = Z_dv.View(aux_size);
    context_->ntt_handler_.NTT(Z_dv_view, np, Z_dv.ConstView(aux_size), true);

    // 8. Construct EvaluationKey on device
    if (aks_key_map_.find(rot) != aks_key_map_.end()) {
      aks_key_map_.erase(rot);
    }
    aks_key_map_.try_emplace(rot, np, beta);
    Evk &evk = aks_key_map_.at(rot);

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
  std::map<int, Plaintext<word>> diagonals;
  for (const auto &[rot, diag] : matrix) {
    rot_indices.push_back(rot);
    diagonals.try_emplace(rot, np);
    context_->encoder_.Encode(diagonals.at(rot), level, scale, diag);
  }
  PrepareAKSKeys(rot_indices, diagonals, level, from_sparse);
}
#endif

template class UserInterface<uint32_t>;
template class UserInterface<uint64_t>;

}  // namespace cheddar