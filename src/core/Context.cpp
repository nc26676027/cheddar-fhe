#include "core/Context.h"

#include "common/Assert.h"
#include "common/CommonUtils.h"
#include "common/PrimeUtils.h"

namespace cheddar {

template <typename word>
void Context<word>::MatchResultWith(Ct &res, const Ct &a) const {
  // The ordering is important here.
  if (a.HasRx()) {
    res.ModifyNP(a.GetNP());
    res.PrepareRx();
  } else {
    res.RemoveRx();
    res.ModifyNP(a.GetNP());
  }
}

template <typename word>
void Context<word>::MatchResultWith(Ct &res, const Ct &a, const Ct &b) const {
  if (a.HasRx() || b.HasRx()) {
    res.ModifyNP(a.GetNP());
    res.PrepareRx();
  } else {
    res.RemoveRx();
    res.ModifyNP(a.GetNP());
  }
}

template <typename word>
DvConstView<word> Context<word>::GetPProd(const NPInfo &np) const {
  int prime_offset = param_.GetMaxNumTer() - np.num_ter_;
  int num_q_primes = np.GetNumQ();
  return DvConstView<word>(p_prod_.data() + prime_offset, num_q_primes);
}

template <typename word>
const ModSwitchHandler<word> &Context<word>::GetDtSModSwitchHandler() const {
  AssertTrue(param_.IsUsingSparseSecretEncapsulation(),
             "Sparse secret encapsulation is not enabled");
  return mod_switch_handlers_.back();
}

template <typename word>
const ModSwitchHandler<word> &Context<word>::GetStDModSwitchHandler() const {
  AssertTrue(param_.IsUsingSparseSecretEncapsulation(),
             "Sparse secret encapsulation is not enabled");
  return mod_switch_handlers_.at(param_.max_key_switch_level_);
}

template <typename word>
void Context<word>::AssertSameScale(const double &scale1,
                                    const double &scale2) const {
  static constexpr double kScaleErrorMargin = 1e-12;
  double diff = scale1 - scale2;
  diff = diff < 0 ? -diff : diff;
  AssertTrue(diff < kScaleErrorMargin * scale1, "Scale mismatch");
}

template <typename word>
std::shared_ptr<Context<word>> Context<word>::Create(
    const Parameter<word> &param) {
  return std::shared_ptr<Context<word>>(new Context<word>(param));
}

template <typename word>
Context<word>::Context(const Parameter<word> &param)
    : param_{param},
      memory_pool_(param_),
      elem_handler_(param_),
      ntt_handler_(param_),
      encoder_(param_, ntt_handler_) {
  // 0. Set some static variables
  Container<word>::SetDegree(param_.degree_);
  MultiLevelCiphertext<word>::StaticInit(param_, encoder_);
  if (!param_.entry_primes_.empty()) {
    AssertTrue(param_.IsUsingSparseSecretEncapsulation(),
               "Low-q0P entry requires sparse secret encapsulation");
    entry_gadget_ = std::make_unique<EntryGadget<word>>(param_);
  }

  // 1. Initialize mod_switch_handlers_
  for (int level = 0; level <= param_.max_level_; level++) {
    mod_switch_handlers_.emplace_back(param_, level, elem_handler_,
                                      ntt_handler_);
  }

  // 2. Initialize p_prod_
  NPInfo np = param_.LevelToNP(param_.max_level_, param_.alpha_);
  std::vector<word> primes = param_.GetPrimeVector(np);
  int num_q_primes = np.GetNumQ();
  HostVector<word> h_p_prod(num_q_primes, 1);
  for (int i = 0; i < num_q_primes; i++) {
    word mod_prime = primes[i];
    for (int j = 0; j < np.num_aux_; j++) {
      h_p_prod[i] =
          primeutil::MultMod(h_p_prod[i], primes[num_q_primes + j], mod_prime);
    }
    h_p_prod[i] = primeutil::ToMontgomery(h_p_prod[i], mod_prime);
  }
  CopyHostToDevice(p_prod_, h_p_prod);

  if (!param_.IsUsingSparseSecretEncapsulation()) return;

  // 3. Initialize level_down_consts_
  level_down_consts_.resize(param_.default_encryption_level_ + 1);
  for (int i = param_.default_encryption_level_; i > 0; i--) {
    double scale = param_.GetScale(i);
    encoder_.EncodeConstant(level_down_consts_[i], i, scale, 1.0);
  }

  // Extra data for SSE
  mod_switch_handlers_.emplace_back(param_, -1, elem_handler_, ntt_handler_);
  np = param_.LevelToNP(-1, param_.GetSSENumAux());
  primes = param_.GetPrimeVector(np);
  num_q_primes = np.GetNumQ();
  HostVector<word> h_p_prod_dts(num_q_primes, 1);
  for (int i = 0; i < num_q_primes; i++) {
    word mod_prime = primes[i];
    for (int j = 0; j < np.num_aux_; j++) {
      h_p_prod_dts[i] = primeutil::MultMod(h_p_prod_dts[i],
                                           primes[num_q_primes + j], mod_prime);
    }
    h_p_prod_dts[i] = primeutil::ToMontgomery(h_p_prod_dts[i], mod_prime);
  }
  CopyHostToDevice(p_prod_dts_, h_p_prod_dts);
}

template <typename word>
Context<word>::~Context() {
  MultiLevelCiphertext<word>::StaticDestroy();
}

template <typename word>
void Context<word>::Copy(Ct &res, const Ct &a) const {
  if (&res == &a) return;
  MatchResultWith(res, a);
  res.SetNumSlots(a.GetNumSlots());
  res.SetScale(a.GetScale());

  CopyDeviceToDevice(res.bx_, a.bx_);
  CopyDeviceToDevice(res.ax_, a.ax_);
  if (a.HasRx()) {
    CopyDeviceToDevice(res.rx_, a.rx_);
  }
}

template <typename word>
void Context<word>::Add(Ct &res, const Ct &a, const Ct &b) const {
  AssertSameNP(a, b);
  AssertSameScale(a, b);
  bool rx_add = a.HasRx() && b.HasRx();
  MatchResultWith(res, a, b);
  res.SetNumSlots(Max(a.GetNumSlots(), b.GetNumSlots()));
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  if (rx_add) {
    auto res_temp = res.ViewVector();
    elem_handler_.Add(res_temp, np, a.ConstViewVector(), b.ConstViewVector());
  } else {
    auto res_temp = res.ViewVector(0, true);
    elem_handler_.Add(res_temp, np, a.ConstViewVector(0, true),
                      b.ConstViewVector(0, true));
    if (a.HasRx()) {
      CopyDeviceToDevice(res.rx_, a.rx_);
    } else if (b.HasRx()) {
      CopyDeviceToDevice(res.rx_, b.rx_);
    }
  }
}

template <typename word>
void Context<word>::Add(Ct &res, const Ct &a, const Pt &b) const {
  AssertSameNP(a, b);
  AssertSameScale(a, b);
  MatchResultWith(res, a);
  res.SetNumSlots(Max(a.GetNumSlots(), b.GetNumSlots()));
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = std::vector<DvView<word>>{res.BxView()};
  elem_handler_.Add(res_temp, np, {a.BxConstView()}, {b.ConstView()});
  CopyDeviceToDevice(res.ax_, a.ax_);
  if (a.HasRx()) {
    CopyDeviceToDevice(res.rx_, a.rx_);
  }
}

template <typename word>
void Context<word>::Add(Ct &res, const Ct &a, const Const &b) const {
  AssertSameNP(a, b);
  AssertSameScale(a, b);
  MatchResultWith(res, a);
  res.SetNumSlots(a.GetNumSlots());
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = std::vector<DvView<word>>{res.BxView()};
  elem_handler_.AddConst(res_temp, np, {a.BxConstView()}, b.ConstView());
  CopyDeviceToDevice(res.ax_, a.ax_);
  if (a.HasRx()) {
    CopyDeviceToDevice(res.rx_, a.rx_);
  }
}

template <typename word>
void Context<word>::Sub(Ct &res, const Ct &a, const Ct &b) const {
  AssertSameNP(a, b);
  AssertSameScale(a, b);
  bool rx_sub = a.HasRx() && b.HasRx();
  MatchResultWith(res, a, b);
  res.SetNumSlots(Max(a.GetNumSlots(), b.GetNumSlots()));
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  if (rx_sub) {
    auto res_temp = res.ViewVector();
    elem_handler_.Sub(res_temp, np, a.ConstViewVector(), b.ConstViewVector());
  } else {
    auto res_temp = res.ViewVector(0, true);
    elem_handler_.Sub(res_temp, np, a.ConstViewVector(0, true),
                      b.ConstViewVector(0, true));
    if (a.HasRx()) {
      CopyDeviceToDevice(res.rx_, a.rx_);
    } else if (b.HasRx()) {
      auto res_temp = std::vector<DvView<word>>{res.RxView()};
      elem_handler_.Neg(res_temp, np, {b.RxConstView()});
    }
  }
}

template <typename word>
void Context<word>::Sub(Ct &res, const Ct &a, const Pt &b) const {
  AssertSameNP(a, b);
  AssertSameScale(a, b);
  MatchResultWith(res, a);
  res.SetNumSlots(Max(a.GetNumSlots(), b.GetNumSlots()));
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = std::vector<DvView<word>>{res.BxView()};
  elem_handler_.Sub(res_temp, np, {a.BxConstView()}, {b.ConstView()});
  CopyDeviceToDevice(res.ax_, a.ax_);
  if (a.HasRx()) {
    CopyDeviceToDevice(res.rx_, a.rx_);
  }
}

template <typename word>
void Context<word>::Sub(Ct &res, const Ct &a, const Const &b) const {
  AssertSameNP(a, b);
  AssertSameScale(a, b);
  MatchResultWith(res, a);
  res.SetNumSlots(a.GetNumSlots());
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = std::vector<DvView<word>>{res.BxView()};
  elem_handler_.SubConst(res_temp, np, {a.BxConstView()}, b.ConstView());
  CopyDeviceToDevice(res.ax_, a.ax_);
  if (a.HasRx()) {
    CopyDeviceToDevice(res.rx_, a.rx_);
  }
}

template <typename word>
void Context<word>::Sub(Ct &res, const Pt &a, const Ct &b) const {
  AssertSameNP(a, b);
  AssertSameScale(a, b);
  MatchResultWith(res, b);
  res.SetNumSlots(Max(a.GetNumSlots(), b.GetNumSlots()));
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = std::vector<DvView<word>>{res.BxView()};
  elem_handler_.Sub(res_temp, np, {a.ConstView()}, {b.BxConstView()});
  if (b.HasRx()) {
    auto res_temp = std::vector<DvView<word>>{res.AxView(), res.RxView()};
    elem_handler_.Neg(res_temp, np, {b.AxConstView(), b.RxConstView()});
  } else {
    auto res_temp = std::vector<DvView<word>>{res.AxView()};
    elem_handler_.Neg(res_temp, np, {b.AxConstView()});
  }
}

template <typename word>
void Context<word>::Sub(Ct &res, const Const &a, const Ct &b) const {
  AssertSameNP(a, b);
  AssertSameScale(a, b);
  MatchResultWith(res, b);
  res.SetNumSlots(b.GetNumSlots());
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = std::vector<DvView<word>>{res.BxView()};
  elem_handler_.SubOppositeConst(res_temp, np, {b.BxConstView()},
                                 a.ConstView());
  if (b.HasRx()) {
    auto res_temp = std::vector<DvView<word>>{res.AxView(), res.RxView()};
    elem_handler_.Neg(res_temp, np, {b.AxConstView(), b.RxConstView()});
  } else {
    auto res_temp = std::vector<DvView<word>>{res.AxView()};
    elem_handler_.Neg(res_temp, np, {b.AxConstView()});
  }
}

template <typename word>
void Context<word>::Neg(Ct &res, const Ct &a) const {
  MatchResultWith(res, a);
  res.SetNumSlots(a.GetNumSlots());
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = res.ViewVector();
  elem_handler_.Neg(res_temp, np, a.ConstViewVector());
}

template <typename word>
void Context<word>::Mult(Ct &res, const Ct &a, const Ct &b) const {
  AssertSameNP(a, b);
  AssertFalse(a.HasRx() || b.HasRx(),
              "Relinearization required before Mult Ct x Ct");
  res.ModifyNP(a.GetNP());
  res.PrepareRx();
  res.SetNumSlots(Max(a.GetNumSlots(), b.GetNumSlots()));
  res.SetScale(a.GetScale() * b.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = res.ViewVector();
  elem_handler_.Tensor(res_temp, np, a.ConstViewVector(0, true),
                       b.ConstViewVector(0, true));
}

template <typename word>
void Context<word>::Mult(Ct &res, const Ct &a, const Pt &b) const {
  AssertSameNP(a, b);
  MatchResultWith(res, a);
  res.SetNumSlots(Max(a.GetNumSlots(), b.GetNumSlots()));
  res.SetScale(a.GetScale() * b.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = res.ViewVector();
  elem_handler_.PMult(res_temp, np, a.ConstViewVector(), b.ConstView());
}

template <typename word>
void Context<word>::Mult(Ct &res, const Ct &a, const Const &b) const {
  AssertSameNP(a, b);
  MatchResultWith(res, a);
  res.SetNumSlots(a.GetNumSlots());
  res.SetScale(a.GetScale() * b.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = res.ViewVector();
  elem_handler_.MultConst(res_temp, np, a.ConstViewVector(), b.ConstView());
}

template <typename word>
bool Context<word>::IsMultUnsafeCompatible(int level1, int level2) const {
  if (level1 == level2) return true;

  int min_level = Min(level1, level2);
  int max_level = Max(level1, level2);

  NPInfo min_np = param_.LevelToNP(min_level, 0);
  NPInfo max_np = param_.LevelToNP(max_level, 0);

  return min_np.IsSubsetOf(max_np);
}

template <typename word>
void Context<word>::MultUnsafe(Ct &res, const Ct &a, const Ct &b,
                               int level) const {
  AssertFalse(a.HasRx() || b.HasRx(),
              "Relinearization required before MultUnsafe Ct x Ct");
  const NPInfo &a_np = a.GetNP();
  const NPInfo &b_np = b.GetNP();
  AssertTrue(a_np.num_aux_ == 0 && b_np.num_aux_ == 0,
             "MultUnsafe Ct x Ct should be only used for ciphertexts without "
             "aux primes");
  int a_level = param_.NPToLevel(a_np);
  int b_level = param_.NPToLevel(b_np);
  if (level == -1) {
    level = Min(a_level, b_level);
  }

  // In-place operation are not possible if the levels are different
  if ((&res == &a && a_level != level) || (&res == &b && b_level != level)) {
    Ct tmp;
    MultUnsafe(tmp, a, b, level);
    res = std::move(tmp);
    return;
  }

  // Target NPInfo
  NPInfo res_np = param_.LevelToNP(level, 0);
  AssertTrue(res_np.IsSubsetOf(a_np) && res_np.IsSubsetOf(b_np),
             "Incompatible levels for MultUnsafe Ct x Ct");

  // Actual computation
  res.ModifyNP(res_np);
  res.PrepareRx();
  res.SetNumSlots(Max(a.GetNumSlots(), b.GetNumSlots()));
  res.SetScale(a.GetScale() * b.GetScale());

  int a_front_ignore = a_np.num_ter_ - res_np.num_ter_;
  int b_front_ignore = b_np.num_ter_ - res_np.num_ter_;

  auto res_temp = res.ViewVector();
  elem_handler_.Tensor(res_temp, res_np,
                       a.ConstViewVector(a_front_ignore, true),
                       b.ConstViewVector(b_front_ignore, true));
}

template <typename word>
void Context<word>::MultUnsafe(Ct &res, const Ct &a, const Pt &b,
                               int level) const {
  const NPInfo &a_np = a.GetNP();
  const NPInfo &b_np = b.GetNP();
  int a_level = param_.NPToLevel(a_np);
  int b_level = param_.NPToLevel(b_np);
  if (level == -1) {
    level = Min(a_level, b_level);
  }

  // In-place operation are not possible if the levels are different
  if (&res == &a && a_level != level) {
    Ct tmp;
    MultUnsafe(tmp, a, b, level);
    res = std::move(tmp);
    return;
  }

  // Target NPInfo
  int num_aux = Min(a_np.num_aux_, b_np.num_aux_);
  NPInfo res_np = param_.LevelToNP(level, num_aux);
  AssertTrue(res_np.IsSubsetOf(a_np) && res_np.IsSubsetOf(b_np),
             "Incompatible levels for MultUnsafe Ct x Pt");

  // Actual computation
  if (a.HasRx()) {
    res.ModifyNP(res_np);
    res.PrepareRx();
  } else {
    res.RemoveRx();
    res.ModifyNP(res_np);
  }
  res.SetNumSlots(Max(a.GetNumSlots(), b.GetNumSlots()));
  res.SetScale(a.GetScale() * b.GetScale());

  int a_front_ignore = a_np.num_ter_ - res_np.num_ter_;
  int b_front_ignore = b_np.num_ter_ - res_np.num_ter_;

  auto res_temp = res.ViewVector();
  elem_handler_.PMult(res_temp, res_np, a.ConstViewVector(a_front_ignore),
                      b.ConstView(b_front_ignore));
}

template <typename word>
void Context<word>::MultUnsafe(Ct &res, const Ct &a, const Const &b,
                               int level) const {
  const NPInfo &a_np = a.GetNP();
  const NPInfo &b_np = b.GetNP();
  int a_level = param_.NPToLevel(a_np);
  int b_level = param_.NPToLevel(b_np);
  if (level == -1) {
    level = Min(a_level, b_level);
  }

  // In-place operation are not possible if the levels are different
  if (&res == &a && a_level != level) {
    Ct tmp;
    MultUnsafe(tmp, a, b, level);
    res = std::move(tmp);
    return;
  }

  // Target NPInfo
  int num_aux = Min(a_np.num_aux_, b_np.num_aux_);
  NPInfo res_np = param_.LevelToNP(level, num_aux);
  AssertTrue(res_np.IsSubsetOf(a_np) && res_np.IsSubsetOf(b_np),
             "Incompatible levels for MultUnsafe Ct x Const");

  // Actual computation
  if (a.HasRx()) {
    res.ModifyNP(res_np);
    res.PrepareRx();
  } else {
    res.RemoveRx();
    res.ModifyNP(res_np);
  }
  res.SetNumSlots(a.GetNumSlots());
  res.SetScale(a.GetScale() * b.GetScale());

  int a_front_ignore = a_np.num_ter_ - res_np.num_ter_;
  int b_front_ignore = b_np.num_ter_ - res_np.num_ter_;

  auto res_temp = res.ViewVector();
  elem_handler_.MultConst(res_temp, res_np, a.ConstViewVector(a_front_ignore),
                          b.ConstView(b_front_ignore));
}

template <typename word>
void Context<word>::Permute(Ct &res, const Ct &a, int rot_dist) const {
  int num_slots = a.GetNumSlots();
  int rot_idx = rot_dist % num_slots;
  if (rot_idx < 0) rot_idx += num_slots;

  if (rot_idx == 0) {
    Copy(res, a);
    return;
  }
  // in-place operation is not supported
  if (&res == &a) {
    Ct tmp;
    Permute(tmp, a, rot_idx);
    res = std::move(tmp);
    return;
  }

  MatchResultWith(res, a);
  res.SetNumSlots(a.GetNumSlots());
  res.SetScale(a.GetScale());
  NPInfo np = a.GetNP();

  auto res_temp = res.ViewVector();
  elem_handler_.Permute(res_temp, np, rot_idx, a.ConstViewVector());
  // elem_handler_.PermuteAccum(res_temp, np, {rot_idx}, {a.ConstViewVector()});
}

template <typename word>
void Context<word>::PermuteConjugate(Ct &res, const Ct &a) const {
  static constexpr int conj_rot_idx = -1;
  // in-place operation is not supported
  if (&res == &a) {
    Ct tmp;
    PermuteConjugate(tmp, a);
    res = std::move(tmp);
    return;
  }

  MatchResultWith(res, a);
  res.SetNumSlots(a.GetNumSlots());
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = res.ViewVector();
  elem_handler_.Permute(res_temp, np, conj_rot_idx, a.ConstViewVector());
}

template <typename word>
void Context<word>::MultImaginaryUnit(Ct &res, const Ct &a) const {
  MatchResultWith(res, a);
  res.SetNumSlots(a.GetNumSlots());
  res.SetScale(a.GetScale());
  NPInfo np = res.GetNP();

  auto res_temp = res.ViewVector();
  elem_handler_.MultImaginaryUnit(res_temp, np, a.ConstViewVector(),
                                  ntt_handler_.ImaginaryUnitConstView(np));
}

template <typename word>
void Context<word>::Relinearize(Ct &res, const Ct &a, const Evk &key) const {
  AssertTrue(a.HasRx(), "Relinearize requires aux");
  MultKey(res, a, key);
}

template <typename word>
void Context<word>::AdjustLevelForMultKey(int &level, const int num_q,
                                          const int num_aux) const {
  AssertTrue(level <= param_.max_key_switch_level_,
             "Key switching is forbidden at OverModRaise levels");
  if (level == 0) {
    // This still has some issues if alpha == num_q at level 0
    if (num_aux != param_.alpha_) {
      level = -1;  // maybe...
    }
  }
  if (level == -1) {
    AssertTrue(num_aux == num_q, "Invalid setting for DTS");
  } else {
    AssertTrue(num_aux == param_.alpha_,
               "Invalid setting for MultKeyNoModDown");
  }
}

template <typename word>
void Context<word>::MultKey(Ct &res, const Ct &a, const Evk &key) const {
  AssertTrue(!key.IsEntryGadget(), "Entry gadget requires its dedicated evaluator");
  NPInfo np = a.GetNP();
  int level = param_.NPToLevel(np);
  int num_aux = key.GetNP().num_aux_;
  int num_q = np.GetNumQ();
  AdjustLevelForMultKey(level, num_q, num_aux);
  const auto &mod_switcher =
      level == -1 ? GetDtSModSwitchHandler() : mod_switch_handlers_.at(level);

  Ct accum;
  MultKeyNoModDown(accum, a, key);
  mod_switcher.ModDown(res, accum);
}

template <typename word>
void Context<word>::MultKeyNoModDown(Ct &accum, const std::vector<Dv> &a_modup,
                                     const Ct &a_orig, const Evk &key) const {
  AssertTrue(!key.IsEntryGadget(), "Entry gadget requires its dedicated evaluator");
  NPInfo a_orig_np = a_orig.GetNP();
  int level = param_.NPToLevel(a_orig_np);
  int num_main = a_orig_np.num_main_;
  int num_ter = a_orig_np.num_ter_;
  int num_aux = key.GetNP().num_aux_;
  int num_q = num_main + num_ter;
  AdjustLevelForMultKey(level, num_q, num_aux);
  const auto &mod_switcher =
      level == -1 ? GetDtSModSwitchHandler() : mod_switch_handlers_.at(level);
  int prime_offset = mod_switcher.GetPrimeOffset();
  int beta = mod_switcher.GetBeta();

  AssertTrue(&accum != &a_orig,
             "In-place operation is not supported for MultKeyNoModDown");
  AssertTrue(key.GetBeta() >= beta && static_cast<int>(a_modup.size()) == beta,
             "Beta mismatch");

  NPInfo np(num_main, num_ter, num_aux);
  accum.RemoveRx();
  accum.ModifyNP(np);
  accum.SetScale(a_orig.GetScale());
  accum.SetNumSlots(a_orig.GetNumSlots());

  std::vector<DvView<word>> accum_views = accum.ViewVector();
  std::vector<std::vector<DvConstView<word>>> key_views;
  std::vector<DvConstView<word>> modup_view;

  for (int i = 0; i < beta; i++) {
    if (!mod_switcher.IsChunkActive(i)) continue;
    key_views.push_back(key.ConstViewVector(i, prime_offset));
    modup_view.push_back(a_modup.at(i).ConstView(num_aux * param_.degree_));
  }
  elem_handler_.PAccum(accum_views, np, key_views, modup_view);
}

template <typename word>
void Context<word>::MultKeyNoModDown(Ct &accum, const Ct &a,
                                     const Evk &key) const {
  AssertTrue(!key.IsEntryGadget(), "Entry gadget requires its dedicated evaluator");
  NPInfo a_np = a.GetNP();
  AssertTrue(a_np.num_aux_ == 0,
             "MultKeyNoModDown is not supported for ciphertexts with p primes");
  AssertTrue(&accum != &a,
             "In-place operation is not supported for MultKeyNoModDown");

  int num_q = a_np.GetNumQ();
  int level = param_.NPToLevel(a_np);
  int num_aux = key.GetNP().num_aux_;
  AdjustLevelForMultKey(level, num_q, num_aux);
  const auto &mod_switcher =
      level == -1 ? GetDtSModSwitchHandler() : mod_switch_handlers_.at(level);

  std::vector<Dv> mod_up_result;
  DvConstView<word> p_prod =
      (level == -1) ? p_prod_dts_.ConstView(0) : GetPProd(a_np);

  // relinearization or simple mult-key
  if (a.HasRx()) {
    mod_switcher.ModUp(mod_up_result, a.RxConstView());
    MultKeyNoModDown(accum, mod_up_result, a, key);
    // accum.bx_ += p_prod * a.bx_
    // accum.ax_ += p_prod * a.ax_
    DvView<word> accum_bx_view(accum.bx_.data(), num_q * param_.degree_, 0);
    DvView<word> accum_ax_view(accum.ax_.data(), num_q * param_.degree_, 0);
    std::vector<DvView<word>> caccum_res{accum_bx_view, accum_ax_view};
    std::vector<std::vector<DvConstView<word>>> src_const_views;
    src_const_views.push_back(a.ConstViewVector(0, true));
    src_const_views.push_back(accum.ConstViewVector());
    elem_handler_.CAccum(caccum_res, a_np, src_const_views, {p_prod});
  } else {
    mod_switcher.ModUp(mod_up_result, a.AxConstView());
    MultKeyNoModDown(accum, mod_up_result, a, key);
    // accum.bx_ += p_prod * a.bx_
    DvView<word> accum_bx_view(accum.bx_.data(), num_q * param_.degree_, 0);
    std::vector<DvView<word>> caccum_res{accum_bx_view};
    std::vector<std::vector<DvConstView<word>>> src_const_views;
    src_const_views.push_back(std::vector<DvConstView<word>>{a.BxConstView()});
    src_const_views.push_back(
        std::vector<DvConstView<word>>{accum.BxConstView()});
    elem_handler_.CAccum(caccum_res, a_np, src_const_views, {p_prod});
  }
}

template <typename word>
void Context<word>::RelinearizeRescale(Ct &res, const Ct &a,
                                       const Evk &key) const {
  AssertTrue(a.HasRx(), "RelinearizeRescale requires aux");
  if (!merged_relinearize_rescale_) {
    Ct relinearized;
    Relinearize(relinearized, a, key);
    Rescale(res, relinearized);
    return;
  }

  int level = param_.NPToLevel(a.GetNP());

  Ct accum;
  MultKeyNoModDown(accum, a, key);
  mod_switch_handlers_.at(level).ModDownAndRescale(res, accum);
}

template <typename word>
void Context<word>::MultKeyNoModDownBatch(std::vector<Ct> &accums,
                                          const std::vector<const Ct *> &as,
                                          const Evk &key) const {
  int total = as.size();
  AssertTrue(total > 0 && !key.IsEntryGadget(),
             "MultKeyNoModDownBatch requires non-empty batch and normal Evk");
  NPInfo a_np = as[0]->GetNP();
  AssertTrue(a_np.num_aux_ == 0,
             "MultKeyNoModDownBatch is not supported for p primes");
  int num_q = a_np.GetNumQ();
  int level = param_.NPToLevel(a_np);
  int num_aux = key.GetNP().num_aux_;
  AdjustLevelForMultKey(level, num_q, num_aux);
  const auto &mod_switcher =
      level == -1 ? GetDtSModSwitchHandler() : mod_switch_handlers_.at(level);
  int prime_offset = mod_switcher.GetPrimeOffset();
  int beta = mod_switcher.GetBeta();
  AssertTrue(key.GetBeta() >= beta, "Beta mismatch");

  std::vector<std::vector<DvConstView<word>>> key_views;
  for (int i = 0; i < beta; i++) {
    if (!mod_switcher.IsChunkActive(i)) continue;
    key_views.push_back(key.ConstViewVector(i, prime_offset));
  }
  DvConstView<word> p_prod =
      (level == -1) ? p_prod_dts_.ConstView(0) : GetPProd(a_np);
  NPInfo np(a_np.num_main_, a_np.num_ter_, num_aux);

  const int max_chunk = (level >= 26) ? 3 : 6;
  accums.resize(total);
  for (int offset = 0; offset < total; offset += max_chunk) {
    int B = std::min(max_chunk, total - offset);
    std::vector<std::vector<Dv>> mod_up_results(B);
    std::vector<std::vector<DvView<word>>> dst_batch(B);
    std::vector<std::vector<DvConstView<word>>> modup_batch(B);
    std::vector<std::vector<DvConstView<word>>> orig_batch(B);

    for (int b = 0; b < B; ++b) {
      const Ct &a = *as[offset + b];
      Ct &accum = accums[offset + b];
      AssertTrue(a.GetNP() == a_np && &accum != &a,
                 "MultKeyNoModDownBatch NP mismatch or in-place");
      if (a.HasRx()) {
        mod_switcher.ModUp(mod_up_results[b], a.RxConstView());
        const_cast<Ct &>(a).RemoveRx();
        orig_batch[b] = a.ConstViewVector(0, true);
      } else {
        mod_switcher.ModUp(mod_up_results[b], a.AxConstView());
        orig_batch[b] = std::vector<DvConstView<word>>{a.BxConstView()};
      }
      accum.RemoveRx();
      accum.ModifyNP(np);
      accum.SetScale(a.GetScale());
      accum.SetNumSlots(a.GetNumSlots());
      dst_batch[b] = accum.ViewVector();
      for (int i = 0; i < beta; i++) {
        if (!mod_switcher.IsChunkActive(i)) continue;
        modup_batch[b].push_back(
            mod_up_results[b].at(i).ConstView(num_aux * param_.degree_));
      }
    }
    if (kMergePMult && kMergeCMult && key_views.size() <= 6) {
      elem_handler_.KeySwitchPAccumBatch(dst_batch, np, key_views, modup_batch,
                                         orig_batch, p_prod);
    } else {
      for (int b = 0; b < B; ++b) {
        MultKeyNoModDown(accums[offset + b], mod_up_results[b], *as[offset + b],
                         key);
      }
    }
  }
}

template <typename word>
void Context<word>::RelinearizeBatch(const std::vector<Ct *> &res,
                                     const std::vector<const Ct *> &as,
                                     const Evk &key) const {
  int total = as.size();
  AssertTrue(total > 0 && static_cast<int>(res.size()) == total,
             "RelinearizeBatch size mismatch");
  for (const Ct *a : as) AssertTrue(a->HasRx(), "RelinearizeBatch requires aux");
  NPInfo a_np = as[0]->GetNP();
  int level = param_.NPToLevel(a_np);
  int num_aux = key.GetNP().num_aux_;
  AdjustLevelForMultKey(level, a_np.GetNumQ(), num_aux);
  const auto &mod_switcher =
      level == -1 ? GetDtSModSwitchHandler() : mod_switch_handlers_.at(level);
  const int max_chunk = (level >= 26) ? 3 : 6;

  for (int offset = 0; offset < total; offset += max_chunk) {
    int B = std::min(max_chunk, total - offset);
    std::vector<const Ct *> chunk_as(as.begin() + offset,
                                     as.begin() + offset + B);
    std::vector<Ct> accums;
    MultKeyNoModDownBatch(accums, chunk_as, key);
    for (int b = 0; b < B; ++b) {
      mod_switcher.ModDown(*res[offset + b], accums[b]);
      accums[b] = Ct{};
    }
  }
}

template <typename word>
void Context<word>::RelinearizeRescaleBatch(const std::vector<Ct *> &res,
                                            const std::vector<const Ct *> &as,
                                            const Evk &key) const {
  int total = as.size();
  AssertTrue(total > 0 && static_cast<int>(res.size()) == total,
             "RelinearizeRescaleBatch size mismatch");
  for (const Ct *a : as)
    AssertTrue(a->HasRx(), "RelinearizeRescaleBatch requires aux");
  if (!merged_relinearize_rescale_) {
    std::vector<Ct> relinearized(total);
    std::vector<Ct *> relin_ptrs(total);
    std::vector<const Ct *> relin_const_ptrs(total);
    for (int i = 0; i < total; ++i) {
      relin_ptrs[i] = &relinearized[i];
      relin_const_ptrs[i] = &relinearized[i];
    }
    RelinearizeBatch(relin_ptrs, as, key);
    RescaleBatch(res, relin_const_ptrs);
    return;
  }

  int level = param_.NPToLevel(as[0]->GetNP());
  const auto &mod_switcher = mod_switch_handlers_.at(level);
  const int max_chunk = (level >= 26) ? 3 : 6;
  for (int offset = 0; offset < total; offset += max_chunk) {
    int B = std::min(max_chunk, total - offset);
    std::vector<const Ct *> chunk_as(as.begin() + offset,
                                     as.begin() + offset + B);
    std::vector<Ct> accums;
    MultKeyNoModDownBatch(accums, chunk_as, key);
    for (int b = 0; b < B; ++b) {
      mod_switcher.ModDownAndRescale(*res[offset + b], accums[b]);
      accums[b] = Ct{};
    }
  }
}

template <typename word>
void Context<word>::RescaleBatch(const std::vector<Ct *> &res,
                                 const std::vector<const Ct *> &as) const {
  int total = as.size();
  AssertTrue(total > 0 && static_cast<int>(res.size()) == total,
             "RescaleBatch size mismatch");
  int level = param_.NPToLevel(as[0]->GetNP());
  AssertTrue(level > 0, "Not enough q primes to rescale");
  const auto &mod_switcher = mod_switch_handlers_.at(level);
  for (int i = 0; i < total; ++i) {
    mod_switcher.Rescale(*res[i], *as[i]);
  }
}

template <typename word>
void Context<word>::MultUnsafeBatchSharedY(
    const std::vector<Ct *> &res,
    const std::vector<const Ct *> &as,
    const Ct &shared_b,
    int level) const {
  const size_t B = res.size();
  AssertTrue(B == as.size() && B > 0,
             "MultUnsafeBatchSharedY: size mismatch");
  AssertFalse(shared_b.HasRx(),
              "Relinearization required before MultUnsafe Ct x Ct");
  const NPInfo &b_np = shared_b.GetNP();
  AssertTrue(b_np.num_aux_ == 0,
             "MultUnsafe Ct x Ct should only be used without aux primes");

  for (size_t b = 0; b < B; ++b) {
    AssertFalse(as[b]->HasRx(),
                "Relinearization required before MultUnsafe Ct x Ct");
    AssertTrue(as[b]->GetNP().num_aux_ == 0,
               "MultUnsafe Ct x Ct should only be used without aux primes");
  }

  int b_level = param_.NPToLevel(b_np);
  if (level == -1) {
    level = b_level;
    for (size_t b = 0; b < B; ++b) {
      level = Min(level, param_.NPToLevel(as[b]->GetNP()));
    }
  }

  NPInfo res_np = param_.LevelToNP(level, 0);
  AssertTrue(res_np.IsSubsetOf(b_np),
             "Incompatible levels for MultUnsafe Ct x Ct");

  int b_front_ignore = b_np.num_ter_ - res_np.num_ter_;
  auto shared_src2 = shared_b.ConstViewVector(b_front_ignore, true);

  std::vector<std::vector<DvView<word>>> dst_batch(B);
  std::vector<std::vector<DvConstView<word>>> src1_batch(B);
  std::vector<Ct> temps;
  temps.reserve(B);

  for (size_t b = 0; b < B; ++b) {
    Ct *out = res[b];
    const Ct *in = as[b];
    int a_level = param_.NPToLevel(in->GetNP());
    if (out == in && a_level != level) {
      temps.emplace_back();
      out = &temps.back();
    }
    const NPInfo &in_np = in->GetNP();
    AssertTrue(res_np.IsSubsetOf(in_np),
               "Incompatible levels for MultUnsafe Ct x Ct");
    int a_front_ignore = in_np.num_ter_ - res_np.num_ter_;
    src1_batch[b] = in->ConstViewVector(a_front_ignore, true);

    out->ModifyNP(res_np);
    out->PrepareRx();
    out->SetNumSlots(Max(in->GetNumSlots(), shared_b.GetNumSlots()));
    out->SetScale(in->GetScale() * shared_b.GetScale());
    dst_batch[b] = out->ViewVector();
  }

  elem_handler_.TensorSharedY(dst_batch, res_np, src1_batch, shared_src2);

  size_t temp_idx = 0;
  for (size_t b = 0; b < B; ++b) {
    int a_level = param_.NPToLevel(as[b]->GetNP());
    if (res[b] == as[b] && a_level != level) {
      *res[b] = std::move(temps[temp_idx++]);
    }
  }
}

template <typename word>
void Context<word>::Rescale(Ct &res, const Ct &a) const {
  int level = param_.NPToLevel(a.GetNP());
  AssertTrue(level > 0, "Not enough q primes to rescale");
  mod_switch_handlers_.at(level).Rescale(res, a);
}

template <typename word>
void Context<word>::HRot(Ct &res, const Ct &a, const Evk &rot_key,
                         int rot_dist) const {
  int num_slots = a.GetNumSlots();
  rot_dist %= num_slots;
  if (rot_dist < 0) rot_dist += num_slots;
  if (rot_dist == 0) {
    Warn("HRot is not necessary");
    Copy(res, a);
    return;
  }

  Ct tmp;
  MultKey(tmp, a, rot_key);
  Permute(res, tmp, rot_dist);
}

template <typename word>
void Context<word>::HConj(Ct &res, const Ct &a, const Evk &conj_key) const {
  Ct tmp;
  MultKey(tmp, a, conj_key);
  PermuteConjugate(res, tmp);
}

template <typename word>
void Context<word>::SplitRealImag(Ct &real, Ct &imaginary, const Ct &input,
                                  const Evk &conj_key) const {
  Ct conjugate;
  HConj(conjugate, input, conj_key);
  Add(real, input, conjugate);
  Sub(imaginary, conjugate, input);
  MultImaginaryUnit(imaginary, imaginary);
  real.SetNumSlots(input.GetNumSlots());
  imaginary.SetNumSlots(input.GetNumSlots());
  real.SetScale(input.GetScale());
  imaginary.SetScale(input.GetScale());
}

template <typename word>
void Context<word>::HRotAdd(Ct &res, const Ct &a, const Ct &b,
                            const Evk &rot_key, int rot_dist) const {
  AssertSameNP(a, b);
  AssertSameScale(a, b);
  int num_slots = Max(a.GetNumSlots(), b.GetNumSlots());
  rot_dist %= num_slots;
  if (rot_dist < 0) rot_dist += num_slots;
  res.SetNumSlots(num_slots);
  res.SetScale(a.GetScale());
  if (rot_dist == 0) {
    Warn("HRotAdd is not necessary");
    Add(res, a, b);
    return;
  }

  Ct tmp;
  MultKey(tmp, a, rot_key);

  NPInfo np = a.GetNP();
  if (b.HasRx()) {
    res.ModifyNP(np);
    res.PrepareRx();
    CopyDeviceToDevice(res.rx_, b.rx_);
  } else {
    res.RemoveRx();
    res.ModifyNP(np);
  }

  auto res_temp = res.ViewVector();
  elem_handler_.PermuteAccum(
      res_temp, np, {rot_dist},
      {tmp.ConstViewVector(), b.ConstViewVector(0, true)});
}

template <typename word>
void Context<word>::HConjAdd(Ct &res, const Ct &a, const Ct &b,
                             const Evk &conj_key) const {
  AssertSameNP(a, b);
  AssertSameScale(a, b);
  int num_slots = Max(a.GetNumSlots(), b.GetNumSlots());
  res.SetNumSlots(num_slots);
  res.SetScale(a.GetScale());

  Ct tmp;
  MultKey(tmp, a, conj_key);

  NPInfo np = a.GetNP();
  if (b.HasRx()) {
    res.ModifyNP(np);
    res.PrepareRx();
    CopyDeviceToDevice(res.rx_, b.rx_);
  } else {
    res.RemoveRx();
    res.ModifyNP(np);
  }

  auto res_temp = res.ViewVector();
  elem_handler_.PermuteAccum(
      res_temp, np, {-1}, {tmp.ConstViewVector(), b.ConstViewVector(0, true)});
}

template <typename word>
void Context<word>::HMult(Ct &res, const Ct &a, const Ct &b,
                          const Evk &mult_key, bool rescale) const {
  Mult(res, a, b);
  if (rescale) {
    RelinearizeRescale(res, res, mult_key);
  } else {
    Relinearize(res, res, mult_key);
  }
}
template <typename word>
void Context<word>::MadUnsafe(Ct &res, const Ct &a, const Const &b) const {
  const NPInfo &a_np = a.GetNP();
  const NPInfo &b_np = b.GetNP();

  NPInfo res_np = res.GetNP();
  AssertTrue(res_np.IsSubsetOf(a_np) && res_np.IsSubsetOf(b_np),
             "Incompatible levels for MultUnsafe Ct x Const");
  AssertTrue(res.GetNP().num_aux_ == 0 && a.GetNP().num_aux_ == 0,
             "MadUnsafe should be only used for q primes");
  AssertTrue(res.GetNumSlots() == a.GetNumSlots(),
             "MadUnsafe should be only used for the same number of slots");

  AssertSameScale(res, a.GetScale() * b.GetScale());

  if (a.HasRx()) {
    res.PrepareRx();
  }

  int a_front_ignore = a_np.num_ter_ - res_np.num_ter_;
  int b_front_ignore = b_np.num_ter_ - res_np.num_ter_;

  if (a.HasRx()) {
    auto res_view = res.ViewVector();
    auto res_const_view = res.ConstViewVector();
    auto a_view = a.ConstViewVector(a_front_ignore);
    auto b_view = b.ConstView(b_front_ignore);
    elem_handler_.CAccum(res_view, res.GetNP(), {a_view, res_const_view},
                         {b_view});

    // elem_handler_.MadConstThree(res.bx_, res.ax_, res.aux_, a.bx_, a.ax_,
    // a.aux_, b.cx_, num_q_primes);
  } else {
    auto res_view = res.ViewVector(0, true);
    auto res_const_view = res.ConstViewVector(0, true);
    auto a_view = a.ConstViewVector(a_front_ignore, true);
    auto b_view = b.ConstView(b_front_ignore);
    elem_handler_.CAccum(res_view, res.GetNP(), {a_view, res_const_view},
                         {b_view});
    // elem_handler_.MadConstTwo(res.bx_, res.ax_, a.bx_, a.ax_, b.cx_,
    //                           num_q_primes);
  }
}

template <typename word>
void Context<word>::LevelDown(Ct &res, const Ct &a, int target_level) const {
  Ct mult_temp, next;
  int level = param_.NPToLevel(a.GetNP());
  const Ct *prev_res = &a;
  AssertTrue(level >= target_level, "Invalid target level for LevelDown");
  for (int i = level; i > target_level; i--) {
    Mult(mult_temp, *prev_res, level_down_consts_[i]);
    Rescale(next, mult_temp);
    prev_res = &next;
  }
  Copy(res, *prev_res);
}

template <typename word>
void Context<word>::DropLevel(Ct &res, const Ct &a, int target_level) const {
  const auto target = param_.LevelToNP(target_level);
  const auto source = a.GetNP();
  AssertTrue(source.num_aux_ == 0 && target.IsSubsetOf(source),
             "DropLevel requires a nested Q-only basis");
  if (&res == &a) {
    Ct tmp;
    DropLevel(tmp, a, target_level);
    res = std::move(tmp);
    return;
  }
  res.RemoveRx();
  res.ModifyNP(target);
  if (a.HasRx()) res.PrepareRx();
  res.SetScale(a.GetScale());
  res.SetNumSlots(a.GetNumSlots());
  const int offset = (source.num_ter_ - target.num_ter_) * param_.degree_;
  const size_t bytes = size_t(target.GetNumQ()) * param_.degree_ * sizeof(word);
  AssertTrue(cudaMemcpyAsync(res.bx_.data(), a.bx_.data() + offset, bytes,
                            cudaMemcpyDeviceToDevice) == cudaSuccess,
             "DropLevel Bx copy failed");
  AssertTrue(cudaMemcpyAsync(res.ax_.data(), a.ax_.data() + offset, bytes,
                            cudaMemcpyDeviceToDevice) == cudaSuccess,
             "DropLevel Ax copy failed");
  if (a.HasRx()) {
    AssertTrue(cudaMemcpyAsync(res.rx_.data(), a.rx_.data() + offset, bytes,
                              cudaMemcpyDeviceToDevice) == cudaSuccess,
               "DropLevel Rx copy failed");
  }
}

template <typename word>
void Context<word>::AddLowerLevelsUntil(MultiLevelCiphertext<word> &ml_ct,
                                        int min_level) const {
  if (ml_ct.Exists(min_level)) {
    return;
  }
  int max_level = ml_ct.GetMaxLevel();
  int old_min_level = ml_ct.GetMinLevel();

  AssertTrue(min_level <= max_level && min_level >= 0,
             "AddLowerLevelsUntil: Invalid level " + std::to_string(min_level));

  Ct tmp;
  for (int i = old_min_level - 1; i >= min_level; i--) {
    ml_ct.AllocateLevel(i);
    Mult(tmp, ml_ct.AtLevel(i + 1),
         MultiLevelCiphertext<word>::GetLevelDownConst(i + 1));
    Rescale(ml_ct.AtLevel(i), tmp);
    ml_ct.DropLevel(i + 1);
  }
}

template class Context<uint32_t>;
template class Context<uint64_t>;

}  // namespace cheddar
