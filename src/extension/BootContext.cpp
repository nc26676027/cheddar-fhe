#include "extension/BootContext.h"

#include <array>
#include <chrono>

#include <cmath>

#include "common/Assert.h"
#include "common/CommonUtils.h"
#include "common/PrimeUtils.h"

namespace {

int Log2Scale(double scale) {
  // round log2(scale)
  return static_cast<int>(std::log2(scale) + 0.5);
}

}  // namespace

namespace cheddar {

template <typename word>
ContextPtr<word> BootContext<word>::GetContext() {
  return this->shared_from_this();
}

template <typename word>
ConstContextPtr<word> BootContext<word>::GetContext() const {
  return this->shared_from_this();
}

template <typename word>
std::shared_ptr<BootContext<word>> BootContext<word>::Create(
    const Parameter<word> &param, const BootParameter &boot_param) {
  return std::shared_ptr<BootContext<word>>(
      new BootContext<word>(param, boot_param));
}

template <typename word>
BootContext<word>::BootContext(const Parameter<word> &param,
                               const BootParameter &boot_param,
                               std::optional<Identity16Route> selected_route)
    : Base(param), selected_route_(selected_route), boot_param_{boot_param} {
  if (selected_route_) {
    AssertTrue(sizeof(word) == 8, "Selected Identity16 requires uint64");
    return;
  }
  // Check if param and boot_param is consistent
  AssertTrue(
      ((!boot_param.use_meboot_cts_ &&
        boot_param.max_level_ == param.max_key_switch_level_) ||
       (boot_param.use_meboot_cts_ &&
        boot_param.max_level_ == param.max_level_ &&
        boot_param.max_level_ == param.max_key_switch_level_ + 1)) &&
          (param.default_encryption_level_ == boot_param.GetStCStartLevel() ||
           (boot_param.use_meboot_cts_ &&
            param.default_encryption_level_ + 2 == boot_param.GetStCStartLevel())),
      "Parameter mismatch for BootContext");

  // At level 0, the scale is adjusted
  // level_zero_scale --> level_zero_scale * 2^log_scaleup_
  // which is approximately equal to q0 / message_ratio;
  // Bootstrapping assumes message is in range (-1, 1) and
  // scale : q0 = 1 : message_ratio
  // to enable correct operation in the approximate eval mod 1 operation
  // this adjustment at level 0 is for fully utilizing the available data
  // range
  double level_zero_scale = param.base_scale_;
  int log_level_zero_scale = Log2Scale(level_zero_scale);

  std::vector<word> q0 = param.GetPrimeVector(param.LevelToNP(-1));
  double q0_prod = 1.0;
  for (word q : q0) {
    q0_prod *= q;
  }
  int log_q0_prod = Log2Scale(q0_prod);
  log_scaleup_ =
      (log_q0_prod - log_level_zero_scale) - boot_param.log_message_ratio_;
  AssertTrue(log_scaleup_ >= 0, "Invalid level zero scaleup");
  level_zero_scale *= (1 << log_scaleup_);

  AssertTrue(log_q0_prod <= 62, "Invalid q0_prod");
  AssertTrue(log_level_zero_scale <= 62, "Invalid level_zero_scale");

  double eval_mod_start_scale = EvalMod<word>::ComputeStartScale(param, boot_param);
  int actual_K = (1 << boot_param.num_double_angle_) * boot_param.initial_K_;

  double eval_mod_end_scale = eval_mod_start_scale;
  int eval_mod_levels = boot_param.GetNumEvalModLevels();
  int eval_mod_start_level = boot_param.GetEvalModStartLevel();
  for (int i = 0; i < eval_mod_levels; i++) {
    eval_mod_end_scale =
        eval_mod_end_scale * eval_mod_end_scale /
        param.GetRescalePrimeProd(eval_mod_start_level - i);
  }

  // See the development notes for details
  cts_const_ = eval_mod_start_scale / (this->param_.degree_ * q0_prod * actual_K);
  stc_const_ = (q0_prod * param.GetScale(boot_param.GetEndLevel())) /
               (eval_mod_end_scale * level_zero_scale);

  this->encoder_.EncodeConstant(scaleup_const_, -1, 1.0, (1 << log_scaleup_));

  // Populating mod_max_intt_const_;

  int num_base = q0.size();
  HostVector<word> intt_const(num_base, 1);
  for (int i = 0; i < num_base; i++) {
    word mod_prime = q0[i];
    for (int j = 0; j < num_base; j++) {
      if (i != j)
        intt_const[i] = primeutil::MultMod(intt_const[i], q0[j], mod_prime);
    }
    intt_const[i] = primeutil::MultMod(
        intt_const[i], static_cast<word>(this->param_.degree_), mod_prime);
    intt_const[i] = primeutil::InvMod(intt_const[i], mod_prime);
  }
  CopyHostToDevice(mod_max_intt_const_, intt_const);
}

template <typename word>
std::shared_ptr<BootContext<word>> BootContext<word>::CreateSelected(
    const Parameter<word> &param, Identity16Route route) {
  auto context = std::shared_ptr<BootContext<word>>(new BootContext(
      param, BootParameter(param.max_level_, 3, 2, 10, false), route));
  if constexpr (sizeof(word) == 8) context->encoder_.EnableNearestRounding();
  return context;
}

template <typename word>
void BootContext<word>::PrepareSelectedIdentity16() {
  AssertTrue(selected_route_.has_value(), "Selected profile required");
  if constexpr (sizeof(word) == 8) {
    if (!selected_execution_) {
      this->EnableCenteredMergedRescale();
      selected_execution_ = std::make_unique<Identity16Execution>(GetContext(), *selected_route_);
      selected_execution_->VerifyEncodedLCRBounds();
    }
  } else {
    Fail("Selected Identity16 requires uint64");
  }
}

template <typename word>
const Identity16Execution &BootContext<word>::GetSelectedExecution() const {
  AssertTrue(selected_execution_ != nullptr, "Selected Identity16 not prepared");
  return *selected_execution_;
}

template <typename word>
double BootContext<word>::GetCtSConst() const {
  return cts_const_;
}

template <typename word>
double BootContext<word>::GetStCConst(BootVariant variant) const {
  return (variant == BootVariant::kImaginaryRemoving ||
          variant == BootVariant::kMergeTwoReal)
             ? stc_const_ / 2
             : stc_const_;
}

template <typename word>
int BootContext<word>::GetBootEnabledNumSlots(int num_slots) const {
  int orig_num_slots = num_slots;
  int half_degree = this->param_.degree_ / 2;
  AssertTrue(num_slots <= half_degree, "num_slots exceeds max_num_slots");
  AssertTrue(IsPowOfTwo(num_slots), "Num slots must be power of 2");
  if (!IsBootPrepared(num_slots)) {
    Warn("BootContext not prepared for num slots: " +
         std::to_string(num_slots));
    num_slots *= 2;
    while (num_slots <= half_degree) {
      if (IsBootPrepared(num_slots)) {
        Warn("Using BootContext prepared for num slots: " +
             std::to_string(num_slots));
        break;
      }
      num_slots *= 2;
    }
    if (num_slots > half_degree) {
      Fail("No BootContext available for num slots: " +
           std::to_string(orig_num_slots));
    }
  }
  return num_slots;
}

template <typename word>
void BootContext<word>::PrepareEvalMod() {
  if (selected_route_) { PrepareSelectedIdentity16(); return; }
  if (eval_mod_ != nullptr) {
    Warn("EvalMod already prepared");
    return;
  }
  eval_mod_ = std::make_unique<EvalMod<word>>(GetContext(), boot_param_);
}

template <typename word>
void BootContext<word>::PrepareEvalSpecialFFT(int num_slots,
                                              BootVariant variant) {
  if (selected_route_) {
    AssertTrue(num_slots == Identity16Profile::kSlots && variant == BootVariant::kNormal,
               "Selected Identity16 requires full complex slots");
    PrepareSelectedIdentity16();
    return;
  }
  AssertTrue(IsPowOfTwo(num_slots), "Only power-of-two slots are supported");
  // TODO: Implement PrepareBootConversionMatrices
  eval_fft_.try_emplace(num_slots, GetContext(), boot_param_, num_slots,
                        GetCtSConst(), GetStCConst(variant));
  boot_variant_.try_emplace(num_slots, variant);
  if (boot_param_.use_meboot_cts_) {
    meboot_cts_.try_emplace(num_slots, GetContext(), boot_param_, num_slots,
                            GetCtSConst());
  }
}

template <typename word>
void BootContext<word>::PrepareMEBootCtS(int num_slots) {
  AssertTrue(IsPowOfTwo(num_slots), "Only power-of-two slots are supported");
  meboot_cts_.try_emplace(num_slots, GetContext(), boot_param_, num_slots,
                          GetCtSConst());
}

template <typename word>
const MEBootCtS<word> &BootContext<word>::GetMEBootCtS(int num_slots) const {
  AssertTrue(meboot_cts_.find(num_slots) != meboot_cts_.end(),
             "MEBootCtS not prepared for num_slots: " + std::to_string(num_slots));
  return meboot_cts_.at(num_slots);
}

template <typename word>
bool BootContext<word>::IsBootPrepared(int num_slots) const {
  if (selected_route_) return selected_execution_ && num_slots == Identity16Profile::kSlots;
  if (boot_param_.use_meboot_cts_) {
    return (eval_mod_ != nullptr) &&
           (eval_fft_.find(num_slots) != eval_fft_.end()) &&
           (meboot_cts_.find(num_slots) != meboot_cts_.end());
  }
  return (eval_mod_ != nullptr) &&
         (eval_fft_.find(num_slots) != eval_fft_.end());
}

template <typename word>
void BootContext<word>::AddRequiredRotations(EvkRequest &req, int num_slots,
                                             bool min_ks) const {
  if (selected_route_) {
    AssertTrue(!min_ks && num_slots == Identity16Profile::kSlots,
               "Selected Identity16 requires the prepared full-slot native path");
    GetSelectedExecution().AddRequiredRotations(req);
    return;
  }
  AssertTrue(!boot_param_.use_meboot_cts_ || num_slots == this->param_.degree_ / 2,
             "MEBOOT OverModRaise requires full packing before any key request");
  int max_num_slots = this->param_.degree_ / 2;
  num_slots = GetBootEnabledNumSlots(num_slots);
  // Trace and rotations for possible slot modification after StC
  for (int ns = num_slots; ns < max_num_slots; ns *= 2) {
    req.AddRequest(ns, boot_param_.GetMaxLevel());
  }
  if (boot_param_.use_meboot_cts_) {
    meboot_cts_.at(num_slots).AddRequiredRotations(req, min_ks);
  }
  eval_fft_.at(num_slots).AddRequiredRotations(req, min_ks);
}

template <typename word>
void BootContext<word>::ModUpToMax(Ct &res, const Ct &input,
                                   const EvkMap<word> &evk_map) const {
  const int L = this->param_.LevelToNP(boot_param_.max_level_).GetNumQ();
  const int alpha = this->param_.alpha_;
  const int degree = this->param_.degree_;
  NPInfo np = this->param_.LevelToNP(-1);
  AssertTrue(input.GetNP() == np, "ModUpToMax: input NP mismatch");
  AssertTrue(!input.HasRx(), "ModUpToMax: input has Rx");
  res.RemoveRx();
  res.ModifyNP(np);
  res.SetNumSlots(input.GetNumSlots());
  res.SetScale(input.GetScale());

  bool sse = this->param_.IsUsingSparseSecretEncapsulation();

  // SSE case
  const Ct *working_ct = &input;
  if (sse) {
    // Dense to sparse key-switch
    const auto &dts_key = evk_map.GetDenseToSparseKey();

    // DtS key-switch
    if (this->entry_gadget_) {
      this->entry_gadget_->Switch(res, input, dts_key);
    } else {
      AssertTrue(!dts_key.IsEntryGadget(), "Entry gadget/profile mismatch");
      this->MultKey(res, input, dts_key);
    }
    working_ct = &res;
  }
  // ModUpToMax sequence
  Dv tmp_bx(L * degree);
  int tmp_ax_num_aux = (sse && !boot_param_.use_meboot_cts_) ? alpha : 0;
  Dv tmp_ax((L + tmp_ax_num_aux) * degree);

  DvView<word> res_bx_view = res.BxView();
  DvView<word> res_ax_view = res.AxView();
  DvView<word> tmp_bx_view = tmp_bx.View(0);
  DvView<word> tmp_ax_view = tmp_ax.View(tmp_ax_num_aux * degree);

  // TODO(jongmin.kim): We can remove some redundant NTT here.
  // ModUpToMax(bx)
  NPInfo max_level_np = this->param_.LevelToNP(boot_param_.max_level_);
  this->ntt_handler_.INTTAndMultConst(res_bx_view, np,
                                      working_ct->BxConstView(),
                                      mod_max_intt_const_.ConstView(0));
  this->elem_handler_.ModUpToMax(tmp_bx_view, res.BxConstView(), max_level_np);
  this->ntt_handler_.NTT(tmp_bx_view, max_level_np, tmp_bx.ConstView(0), true);

  // ModUpToMax(ax)
  max_level_np.num_aux_ = tmp_ax_num_aux;
  this->ntt_handler_.INTTAndMultConst(res_ax_view, np,
                                      working_ct->AxConstView(),
                                      mod_max_intt_const_.ConstView(0));
  this->elem_handler_.ModUpToMax(tmp_ax_view, res.AxConstView(), max_level_np);
  this->ntt_handler_.NTT(tmp_ax_view, max_level_np,
                         tmp_ax.ConstView(tmp_ax_num_aux * degree), true);

  if (sse && !boot_param_.use_meboot_cts_) {
    // StD key-switch
    const auto &std_key = evk_map.GetSparseToDenseKey();
    const auto &std_mod_switcher = this->GetStDModSwitchHandler();
    // MultKey
    Ct tmp_std(max_level_np);
    std::vector<DvView<word>> tmp_std_view = tmp_std.ViewVector();
    this->elem_handler_.PMult(tmp_std_view, max_level_np,
                              std_key.ConstViewVector(0),
                              tmp_ax.ConstView(tmp_ax_num_aux * degree));

    // PseudoModUp: tmp_std.bx_ += tmp_bx * p_prod
    DvView<word> tmp_std_bx_view(tmp_std.bx_.data(), L * degree, 0);
    std::vector<DvView<word>> caccum_res{tmp_std_bx_view};
    std::vector<std::vector<DvConstView<word>>> caccum_input;
    caccum_input.push_back(std::vector<DvConstView<word>>{tmp_bx.ConstView(0)});
    caccum_input.push_back(
        std::vector<DvConstView<word>>{tmp_std.BxConstView()});
    max_level_np.num_aux_ = 0;
    this->elem_handler_.CAccum(caccum_res, max_level_np, caccum_input,
                               {this->GetPProd(max_level_np)});
    // ModDown
    tmp_std.SetNumSlots(input.GetNumSlots());
    tmp_std.SetScale(input.GetScale());
    std_mod_switcher.ModDown(res, tmp_std);
  } else {
    res.bx_ = std::move(tmp_bx);
    res.ax_ = std::move(tmp_ax);
    res.ModifyNP(max_level_np);
  }
}

template <typename word>
void BootContext<word>::CoeffToSlot(Ct &res, int num_slots, const Ct &input,
                                    const EvkMap<word> &evk_map,
                                    bool min_ks /*= false*/) const {
  if (boot_param_.use_meboot_cts_) {
    CoeffToSlot(res, num_slots, input, evk_map, evk_map.GetAKSKeyMap(), min_ks);
    return;
  }
  eval_fft_.at(num_slots).EvaluateCtS(GetContext(), res, input, evk_map,
                                      min_ks);
}

template <typename word>
void BootContext<word>::CoeffToSlot(Ct &res, int num_slots, const Ct &input,
                                    const EvkMap<word> &evk_map,
                                    const AKSKeyMap<word> &aks_map,
                                    bool min_ks /*= false*/) const {
  AssertTrue(boot_param_.use_meboot_cts_,
             "CoeffToSlot with AKSKeyMap requires use_meboot_cts to be enabled");
  AssertTrue(meboot_cts_.find(num_slots) != meboot_cts_.end(),
             "MEBootCtS not prepared for num_slots: " + std::to_string(num_slots));
  meboot_cts_.at(num_slots).Evaluate(res, input, evk_map, aks_map, min_ks);
}

template <typename word>
void BootContext<word>::SlotToCoeff(Ct &res, int num_slots, const Ct &input,
                                    const EvkMap<word> &evk_map,
                                    bool min_ks /*= false*/) const {
  eval_fft_.at(num_slots).EvaluateStC(GetContext(), res, input, evk_map,
                                      min_ks);
}

template <typename word>
void BootContext<word>::EvaluateMod(Ct &res, const Ct &input,
                                    const Evk &mult_key) const {
  AssertTrue(eval_mod_ != nullptr, "EvalMod not prepared");
  this->AssertSameScale(input, eval_mod_->start_scale_);
  eval_mod_->Evaluate(GetContext(), res, input, mult_key);
  this->AssertSameScale(res, eval_mod_->end_scale_);
}

template <typename word>
void BootContext<word>::BootImpl(Ct &res, const Ct &input,
                                 const EvkMap<word> &evk_map,
                                 const AKSKeyMap<word> *aks_map,
                                 bool min_ks,
                                 const std::function<void(const char *)> &mark) const {
  if (selected_route_) {
    if constexpr (sizeof(word) == 8) {
      AssertTrue(!min_ks, "Selected Identity16 min-KS is not prepared");
      GetSelectedExecution().Evaluate(res, input, evk_map, mark);
      return;
    } else {
      Fail("Selected Identity16 requires uint64");
    }
  }
  int half_degree = this->param_.degree_ / 2;
  int input_num_slots = input.GetNumSlots();
  AssertTrue(!boot_param_.use_meboot_cts_ ||
                 input_num_slots == this->param_.degree_ / 2,
             "MEBOOT OverModRaise requires full packing before Trace");
  int num_slots = GetBootEnabledNumSlots(input_num_slots);
  bool full_slot = (num_slots == half_degree);
  AssertTrue(eval_mod_ != nullptr, "EvalMod not prepared");

  Ct main_ct;
  int input_level = this->param_.NPToLevel(input.GetNP());
  if (input_level > 0) {
    this->LevelDown(main_ct, input, 0);
    if (mark) mark("LevelDown");
    BootImpl(res, main_ct, evk_map, aks_map, min_ks, mark);
    return;
  }

  // 0. Scale up
  NPInfo min_np = this->param_.LevelToNP(-1);
  AssertTrue(min_np.IsSubsetOf(input.GetNP()), "Boot: Invalid input NP");
  this->MultUnsafe(main_ct, input, scaleup_const_, -1);
  if (mark) mark("ScaleUp");

  // 1. ModUpToMax with optional DtS/StD key-switch + Trace
  ModUpToMax(main_ct, main_ct, evk_map);
  if (mark) mark("ModUp");

  // Perform trace
  main_ct.SetNumSlots(half_degree);
  Trace(main_ct, num_slots, (half_degree / num_slots), main_ct, evk_map);
  main_ct.SetNumSlots(num_slots);
  if (mark) mark("Trace");

  // 2. Perform CtS
  if (aks_map != nullptr) {
    CoeffToSlot(main_ct, num_slots, main_ct, evk_map, *aks_map, min_ks);
  } else {
    CoeffToSlot(main_ct, num_slots, main_ct, evk_map, min_ks);
  }
  if (mark) mark("CtS");

  // 3. Extract real/imag part and perform EvalMod
  main_ct.SetScale(eval_mod_->start_scale_);
  if (full_slot) {
    Ct ct_conj;
    // Perform eval mod on real and imag part separately
    this->SplitRealImag(res, ct_conj, main_ct,
                        evk_map.GetConjugationKey());
    if (mark) mark("SplitRealImag");
    EvaluateMod(res, res, evk_map.GetMultiplicationKey());
    if (mark) mark("EvalModReal");
    EvaluateMod(ct_conj, ct_conj, evk_map.GetMultiplicationKey());
    if (mark) mark("EvalModImag");
    this->MultImaginaryUnit(ct_conj, ct_conj);
    this->Add(res, res, ct_conj);
    if (mark) mark("Recombine");
  } else {
    // Can merge real and imag part using extra slots
    this->HConjAdd(res, main_ct, main_ct, evk_map.GetConjugationKey());
    if (mark) mark("SplitRealImag");
    EvaluateMod(res, res, evk_map.GetMultiplicationKey());
    if (mark) mark("EvalModReal");
  }

  // 4. Finally, perform StC
  SlotToCoeff(res, num_slots, res, evk_map, min_ks);
  if (mark) mark("StC");

  if (boot_variant_.at(num_slots) == BootVariant::kImaginaryRemoving) {
    // res += HConJ(res)
    this->HConjAdd(res, res, res, evk_map.GetConjugationKey());
    if (mark) mark("RemoveImaginary");
  }

  // Restore num slots and set scale (just in case)
  res.SetNumSlots(input_num_slots);
  double final_scale = this->param_.GetScale(boot_param_.GetEndLevel());
  res.SetScale(final_scale);
  if (mark) mark("Finalize");
}

template <typename word>
BootProfile BootContext<word>::ProfileBoot(
    Ct &res, const Ct &input, const EvkMap<word> &evk_map, bool min_ks) const {
  // Pre-create events, so event creation is outside the measured interval.
  struct Events {
    std::array<cudaEvent_t, 16> values{};
    ~Events() {
      for (auto event : values) if (event) cudaEventDestroy(event);
    }
  } events;
  for (auto &event : events.values) {
    AssertTrue(cudaEventCreate(&event) == cudaSuccess, "ProfileBoot: event creation failed");
  }
  BootProfile result;
  result.stages.reserve(events.values.size() - 1);
  size_t cursor = 0;
  cudaDeviceSynchronize();
  auto begin = std::chrono::steady_clock::now();
  cudaEventRecord(events.values[0]);
  auto previous = std::chrono::steady_clock::now();
  auto mark = [&](const char *name) {
    AssertTrue(cursor + 1 < events.values.size(), "ProfileBoot: too many stages");
    cudaEventRecord(events.values[++cursor]);
    auto now = std::chrono::steady_clock::now();
    result.stages.push_back({name, 0, std::chrono::duration<double, std::milli>(
                                        now - previous).count()});
    previous = now;
  };
  const AKSKeyMap<word> *aks = boot_param_.use_meboot_cts_ ? &evk_map.GetAKSKeyMap() : nullptr;
  BootImpl(res, input, evk_map, aks, min_ks, mark);
  mark("ReleaseTemps");
  AssertTrue(cudaEventSynchronize(events.values[cursor]) == cudaSuccess,
             "ProfileBoot: bootstrap failed");
  result.wall_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - begin).count();
  cudaEventElapsedTime(&result.gpu_ms, events.values[0], events.values[cursor]);
  for (size_t i = 0; i < result.stages.size(); ++i) {
    cudaEventElapsedTime(&result.stages[i].gpu_ms, events.values[i], events.values[i + 1]);
  }
  return result;
}

template <typename word>
void BootContext<word>::Boot(Ct &res, const Ct &input,
                             const EvkMap<word> &evk_map, bool min_ks) const {
  const AKSKeyMap<word> *aks_ptr =
      boot_param_.use_meboot_cts_ ? &evk_map.GetAKSKeyMap() : nullptr;
  BootImpl(res, input, evk_map, aks_ptr, min_ks);
}

template <typename word>
void BootContext<word>::Boot(Ct &res, const Ct &input,
                             const EvkMap<word> &evk_map,
                             const AKSKeyMap<word> &aks_map,
                             bool min_ks) const {
  AssertTrue(boot_param_.use_meboot_cts_,
             "Boot with AKSKeyMap requires use_meboot_cts to be enabled");
  BootImpl(res, input, evk_map, &aks_map, min_ks);
}

template <typename word>
void BootContext<word>::Trace(Ct &res, int start_rot_dist, int num_accum,
                              const Ct &input,
                              const EvkMap<word> &evk_map) const {
  int num_slots = input.GetNumSlots();
  AssertTrue(IsPowOfTwo(num_accum), "Num accum must be power of 2");
  int log_num_accum = Log2Ceil(num_accum);

  if (num_accum == 1) {
    this->Copy(res, input);
    return;
  }

  NPInfo np = input.GetNP();
  AssertTrue(np.num_aux_ == 0, "Trace: Aux primes are not allowed");

  res.RemoveRx();
  res.ModifyNP(np);
  res.SetNumSlots(num_slots);
  res.SetScale(input.GetScale());
  std::vector<DvView<word>> res_view = res.ViewVector();

  Ct tmp;
  for (int i = 0; i < log_num_accum; i++) {
    int rot_idx = (start_rot_dist * (1 << i)) % num_slots;
    if (rot_idx < 0) rot_idx += num_slots;
    const auto &evk = evk_map.GetRotationKey(rot_idx);
    if (i == 0) {
      // res = HRot(input, rot_idx) + input
      this->HRotAdd(res, input, input, evk, rot_idx);
    } else {
      // res += HRot(res, rot_idx)
      this->HRotAdd(res, res, res, evk, rot_idx);
    }
  }
}

template class BootContext<uint32_t>;
template class BootContext<uint64_t>;

}  // namespace cheddar
