#include "extension/MEBootCtS.h"

#include "common/Assert.h"
#include "common/CommonUtils.h"

namespace cheddar {

template <typename word>
MEBootCtS<word>::MEBootCtS(ConstContextPtr<word> context, int num_slots,
                           int start_level, double scaling_factor,
                           const std::vector<int> &widths,
                           const std::vector<int> &dense_bases)
    : context_{context},
      num_slots_{num_slots},
      start_level_{start_level},
      scaling_factor_{scaling_factor},
      lcr_scale_{0.0} {
  AssertTrue(start_level_ >= 3, "MEBootCtS requires start_level >= 3");
  AssertTrue(IsPowOfTwo(num_slots_), "num_slots must be a power of 2");
  AssertTrue(widths.size() == 4, "MEBootCtS expects exactly 4 stage widths");
  AssertTrue(dense_bases.size() == 2, "MEBootCtS expects 2 dense bases");

  NPInfo stage1_np = context_->param_.LevelToNP(start_level_ - 1, 0);
  auto q_vec = context_->param_.GetPrimeVector(stage1_np);
  lcr_scale_ = static_cast<double>(q_vec.back());

  plans_ = MEBootMatrixPlanner::PlanCtS(context_, num_slots_, widths,
                                       dense_bases, scaling_factor_);
  AssertTrue(plans_.size() == 4, "PlanCtS must produce 4 stage plans");

  InitializeStages(dense_bases);
}

template <typename word>
MEBootCtS<word>::MEBootCtS(ConstContextPtr<word> context,
                           const BootParameter &boot_param, int num_slots,
                           double scaling_factor,
                           const std::vector<int> &widths,
                           const std::vector<int> &dense_bases)
    : MEBootCtS(context, num_slots, boot_param.GetCtSStartLevel(),
                scaling_factor, widths, dense_bases) {}

template <typename word>
void MEBootCtS<word>::InitializeStages(const std::vector<int> &dense_bases) {
  // Stage 0: evaluated at start_level_, rescales to start_level_ - 1
  double pt_scale0 = context_->param_.GetRescalePrimeProd(start_level_);
  stage0_ = std::make_unique<LinearTransform<word>>(
      context_, plans_[0].matrix, start_level_, pt_scale0, plans_[0].bs,
      plans_[0].gs);

  // Stage 1 (LCR+AKS): evaluated at start_level_ - 1, preserves start_level_ - 1
  stage1_ = std::make_unique<LCROperator<word>>(
      context_, plans_[1].matrix, start_level_ - 1, lcr_scale_);

  // Stage 2: evaluated at start_level_ - 1, rescales to start_level_ - 2
  double pt_scale2 = context_->param_.GetRescalePrimeProd(start_level_ - 1);
  stage2_ = std::make_unique<LinearTransform<word>>(
      context_, plans_[2].matrix, start_level_ - 1, pt_scale2, dense_bases[0]);

  // Stage 3: evaluated at start_level_ - 2, rescales to start_level_ - 3
  double pt_scale3 = context_->param_.GetRescalePrimeProd(start_level_ - 2);
  stage3_ = std::make_unique<LinearTransform<word>>(
      context_, plans_[3].matrix, start_level_ - 2, pt_scale3, dense_bases[1]);
}

template <typename word>
void MEBootCtS<word>::AddRequiredRotations(EvkRequest &req, bool min_ks) const {
  stage0_->AddRequiredRotations(req, min_ks);
  // Stage 1 (LCR) uses AKS keys via PrepareAKSKeys, not standard rotation keys.
  stage2_->AddRequiredRotations(req, min_ks);
  stage3_->AddRequiredRotations(req, min_ks);
}

template <typename word>
void MEBootCtS<word>::EvaluateStage0(Ct &res, const Ct &input,
                                     const EvkMap<word> &evk_map,
                                     bool min_ks) const {
  AssertTrue(context_->param_.NPToLevel(input.GetNP()) == start_level_,
             "MEBootCtS: Stage 0 input level mismatch");
  stage0_->Evaluate(context_, res, input, evk_map, min_ks);
  AssertTrue(context_->param_.NPToLevel(res.GetNP()) == start_level_ - 1,
             "MEBootCtS: Stage 0 output level mismatch");
}

template <typename word>
void MEBootCtS<word>::EvaluateStage1(Ct &res, const Ct &input,
                                     const AKSKeyMap<word> &aks_map) const {
  AssertTrue(context_->param_.NPToLevel(input.GetNP()) == start_level_ - 1,
             "MEBootCtS: Stage 1 input level mismatch");
  stage1_->Evaluate(res, input, aks_map);
  AssertTrue(context_->param_.NPToLevel(res.GetNP()) == start_level_ - 1,
             "MEBootCtS: Stage 1 output level mismatch (level conservation failed)");
}

template <typename word>
void MEBootCtS<word>::EvaluateStage2(Ct &res, const Ct &input,
                                     const EvkMap<word> &evk_map,
                                     bool min_ks) const {
  AssertTrue(context_->param_.NPToLevel(input.GetNP()) == start_level_ - 1,
             "MEBootCtS: Stage 2 input level mismatch");
  stage2_->Evaluate(context_, res, input, evk_map, min_ks);
  AssertTrue(context_->param_.NPToLevel(res.GetNP()) == start_level_ - 2,
             "MEBootCtS: Stage 2 output level mismatch");
}

template <typename word>
void MEBootCtS<word>::EvaluateStage3(Ct &res, const Ct &input,
                                     const EvkMap<word> &evk_map,
                                     bool min_ks) const {
  AssertTrue(context_->param_.NPToLevel(input.GetNP()) == start_level_ - 2,
             "MEBootCtS: Stage 3 input level mismatch");
  stage3_->Evaluate(context_, res, input, evk_map, min_ks);
  AssertTrue(context_->param_.NPToLevel(res.GetNP()) == start_level_ - 3,
             "MEBootCtS: Stage 3 output level mismatch");
}

template <typename word>
void MEBootCtS<word>::Evaluate(Ct &res, const Ct &input,
                               const EvkMap<word> &evk_map,
                               const AKSKeyMap<word> &aks_map,
                               bool min_ks) const {
  Ct ct_stage0;
  EvaluateStage0(ct_stage0, input, evk_map, min_ks);

  Ct ct_stage1;
  EvaluateStage1(ct_stage1, ct_stage0, aks_map);

  Ct ct_stage2;
  EvaluateStage2(ct_stage2, ct_stage1, evk_map, min_ks);

  EvaluateStage3(res, ct_stage2, evk_map, min_ks);
  res.SetNumSlots(num_slots_);
}

template <typename word>
void MEBootCtS<word>::SplitRealImag(Ct &ct_real, Ct &ct_imag, const Ct &ct_in,
                                    const Evk &conj_key) const {
  Ct ct_conj;
  context_->HConj(ct_conj, ct_in, conj_key);
  Ct tmp_real, tmp_imag;
  context_->Add(tmp_real, ct_in, ct_conj);
  context_->Sub(tmp_imag, ct_conj, ct_in);
  context_->MultImaginaryUnit(tmp_imag, tmp_imag);
  tmp_real.SetNumSlots(ct_in.GetNumSlots());
  tmp_imag.SetNumSlots(ct_in.GetNumSlots());
  tmp_real.SetScale(ct_in.GetScale());
  tmp_imag.SetScale(ct_in.GetScale());
  ct_real = std::move(tmp_real);
  ct_imag = std::move(tmp_imag);
}

template class MEBootCtS<uint32_t>;
template class MEBootCtS<uint64_t>;

}  // namespace cheddar
