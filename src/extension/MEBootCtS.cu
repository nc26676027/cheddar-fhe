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

  NPInfo stage2_np = context->param_.LevelToNP(start_level_ - 1, 0);
  auto q_vec = context->param_.GetPrimeVector(stage2_np);
  lcr_scale_ = static_cast<double>(q_vec.back());

  plans_ = MEBootMatrixPlanner::PlanCtS(context, num_slots_, widths,
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
  auto context = GetContext();
  // Stage 1: evaluated at start_level_, rescales to start_level_ - 1
  // Directly pre-encode diagonals: all 16 automorphisms fix the subring secret,
  // so no Galois key-switching is required.
  NPInfo stage1_np = context->param_.LevelToNP(start_level_, 0);
  double pt_scale1 = context->param_.GetRescalePrimeProd(start_level_);
  for (const auto &[rot, diag] : plans_[0].matrix) {
    stage1_diagonals_.try_emplace(rot, stage1_np);
    context->encoder_.Encode(stage1_diagonals_.at(rot), start_level_, pt_scale1,
                              diag);
  }

  // Stage 2 (LCR+AKS): evaluated at start_level_ - 1, preserves start_level_ - 1
  stage2_ = std::make_unique<LCROperator<word>>(
      context, plans_[1].matrix, start_level_ - 1, lcr_scale_);

  // Stage 3: evaluated at start_level_ - 1, rescales to start_level_ - 2
  double pt_scale3 = context->param_.GetRescalePrimeProd(start_level_ - 1);
  stage3_ = std::make_unique<LinearTransform<word>>(
      context, plans_[2].matrix, start_level_ - 1, pt_scale3, dense_bases[0]);

  // Stage 4: evaluated at start_level_ - 2, rescales to start_level_ - 3
  double pt_scale4 = context->param_.GetRescalePrimeProd(start_level_ - 2);
  stage4_ = std::make_unique<LinearTransform<word>>(
      context, plans_[3].matrix, start_level_ - 2, pt_scale4, dense_bases[1]);
}

template <typename word>
void MEBootCtS<word>::AddRequiredRotations(EvkRequest &req, bool min_ks) const {
  (void)min_ks;
  // Stage 1 requires ZERO Galois rotation keys (subring automorphisms strictly fix the subring secret).
  // Stage 2 (LCR) uses AKS keys via PrepareAKSKeys, not standard rotation keys.
  // Stages 3 and 4 use dense BSGS double hoisting (bsgs_ratio=64, 8).
  stage3_->AddRequiredRotations(req, false);
  stage4_->AddRequiredRotations(req, false);
}

template <typename word>
void MEBootCtS<word>::EvaluateStage1(Ct &res, const Ct &input) const {
  auto context = GetContext();
  AssertTrue(context->param_.NPToLevel(input.GetNP()) == start_level_,
             "MEBootCtS: Stage 1 input level mismatch");

  Ct accum;
  bool first = true;
  for (const auto &[rot, diag_pt] : stage1_diagonals_) {
    int rot_offset = rot % num_slots_;
    if (rot_offset < 0) rot_offset += num_slots_;

    Ct ct_rot;
    if (rot_offset == 0) {
      context->Copy(ct_rot, input);
    } else {
      context->Permute(ct_rot, input, rot_offset);
    }

    Ct term;
    context->Mult(term, ct_rot, diag_pt);

    if (first) {
      accum = std::move(term);
      first = false;
    } else {
      context->Add(accum, accum, term);
    }
  }

  context->Rescale(res, accum);
  res.SetNumSlots(num_slots_);
  AssertTrue(context->param_.NPToLevel(res.GetNP()) == start_level_ - 1,
             "MEBootCtS: Stage 1 output level mismatch");
}

template <typename word>
void MEBootCtS<word>::EvaluateStage2(Ct &res, const Ct &input,
                                     const AKSKeyMap<word> &aks_map) const {
  auto context = GetContext();
  AssertTrue(context->param_.NPToLevel(input.GetNP()) == start_level_ - 1,
             "MEBootCtS: Stage 2 input level mismatch");
  stage2_->Evaluate(res, input, aks_map);
  AssertTrue(context->param_.NPToLevel(res.GetNP()) == start_level_ - 1,
             "MEBootCtS: Stage 2 output level mismatch (level conservation failed)");
}

template <typename word>
void MEBootCtS<word>::EvaluateStage3(Ct &res, const Ct &input,
                                     const EvkMap<word> &evk_map,
                                     bool min_ks) const {
  (void)min_ks;
  auto context = GetContext();
  AssertTrue(context->param_.NPToLevel(input.GetNP()) == start_level_ - 1,
             "MEBootCtS: Stage 3 input level mismatch");
  stage3_->Evaluate(context, res, input, evk_map, false);
  AssertTrue(context->param_.NPToLevel(res.GetNP()) == start_level_ - 2,
             "MEBootCtS: Stage 3 output level mismatch");
}

template <typename word>
void MEBootCtS<word>::EvaluateStage4(Ct &res, const Ct &input,
                                     const EvkMap<word> &evk_map,
                                     bool min_ks) const {
  (void)min_ks;
  auto context = GetContext();
  AssertTrue(context->param_.NPToLevel(input.GetNP()) == start_level_ - 2,
             "MEBootCtS: Stage 4 input level mismatch");
  stage4_->Evaluate(context, res, input, evk_map, false);
  AssertTrue(context->param_.NPToLevel(res.GetNP()) == start_level_ - 3,
             "MEBootCtS: Stage 4 output level mismatch");
}

template <typename word>
void MEBootCtS<word>::Evaluate(Ct &res, const Ct &input,
                               const EvkMap<word> &evk_map,
                               const AKSKeyMap<word> &aks_map,
                               bool min_ks) const {
  Ct ct_stage1;
  EvaluateStage1(ct_stage1, input);

  Ct ct_stage2;
  EvaluateStage2(ct_stage2, ct_stage1, aks_map);

  Ct ct_stage3;
  EvaluateStage3(ct_stage3, ct_stage2, evk_map, min_ks);

  EvaluateStage4(res, ct_stage3, evk_map, min_ks);
  res.SetNumSlots(num_slots_);
}

template <typename word>
void MEBootCtS<word>::SplitRealImag(Ct &ct_real, Ct &ct_imag, const Ct &ct_in,
                                    const Evk &conj_key) const {
  auto context = GetContext();
  Ct ct_conj;
  context->HConj(ct_conj, ct_in, conj_key);
  Ct tmp_real, tmp_imag;
  context->Add(tmp_real, ct_in, ct_conj);
  context->Sub(tmp_imag, ct_conj, ct_in);
  context->MultImaginaryUnit(tmp_imag, tmp_imag);
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
