#include "extension/MEBootCtS.h"

#include <type_traits>
#include "common/Assert.h"
#include "common/Basic.cuh"
#include "common/CommonUtils.h"
#include "common/PrimeUtils.h"

namespace cheddar {

namespace {

// The subring secret is fixed by every prefix automorphism. Gather both
// ciphertext components, multiply by the output-position diagonal and reduce
// all 16 terms in registers, without materializing rotated ciphertexts.
template <typename word, int terms>
__global__ void SubringPrefixKernel(
    word *output_b, word *output_a, const word *input_b, const word *input_a,
    const uint64_t *diagonals, const word *galois_factors,
    const word *primes, const word *mont_inverses, int num_q, int log_degree) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= (num_q << log_degree)) return;
  int limb = i >> log_degree;
  uint32_t position = i & ((1 << log_degree) - 1);
  uint32_t root = basic::BitReverse(position, log_degree + 1) + 1;
  word q = primes[limb];
  auto inverse = static_cast<make_signed_t<word>>(mont_inverses[limb]);
  word sum_b = 0, sum_a = 0;
#pragma unroll
  for (int k = 0; k < terms; ++k) {
    uint32_t source = basic::BitReverse(
        root * static_cast<uint32_t>(galois_factors[k]) - 1, log_degree + 1);
    int source_index = (limb << log_degree) + source;
    const word *diagonal = reinterpret_cast<const word *>(diagonals[k]);
    word factor = basic::StreamingLoad(diagonal + i);
    sum_b = basic::Add(sum_b, basic::MultMontgomery(
        basic::StreamingLoad(input_b + source_index), factor, q, inverse), q);
    sum_a = basic::Add(sum_a, basic::MultMontgomery(
        basic::StreamingLoad(input_a + source_index), factor, q, inverse), q);
  }
  output_b[i] = sum_b;
  output_a[i] = sum_a;
}

}  // namespace

template <typename word>
MEBootCtS<word>::MEBootCtS(ConstContextPtr<word> context, int num_slots,
                           int start_level, double scaling_factor,
                           const std::vector<int> &widths,
                           const std::vector<int> &dense_bases,
                           int subring_extension)
    : context_{context},
      num_slots_{num_slots},
      start_level_{start_level},
      scaling_factor_{scaling_factor},
      lcr_scale_{0.0} {
  AssertTrue(start_level_ >= 3, "MEBootCtS requires start_level >= 3");
  AssertTrue(start_level_ - 1 <= context->param_.max_key_switch_level_,
             "MEBootCtS must remove OverModRaise before AKS");
  AssertTrue(IsPowOfTwo(num_slots_), "num_slots must be a power of 2");
  AssertTrue(widths.size() == 4, "MEBootCtS expects exactly 4 stage widths");
  AssertTrue(dense_bases.size() == 2, "MEBootCtS expects 2 dense bases");
  AssertTrue(context->param_.IsUsingSparseSecretEncapsulation(),
             "MEBootCtS requires a subring-supported sparse secret");
  AssertTrue(num_slots_ == context->param_.degree_ / 2 &&
                 ((widths[0] == 4 && subring_extension == 16) ||
                  (widths[0] == 3 && subring_extension == 8)),
             "MEBootCtS requires a bound width-3/ext-8 or width-4/ext-16 prefix");

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
  AssertTrue(stage1_diagonals_.size() == (size_t(1) << plans_[0].width),
             "Subring Prefix diagonal count mismatch");
  HostVector<uint64_t> pointers;
  HostVector<word> galois;
  for (const auto &[rot, diagonal] : stage1_diagonals_) {
    pointers.push_back(reinterpret_cast<uint64_t>(diagonal.ConstView().data()));
    int rotation = rot % num_slots_;
    if (rotation < 0) rotation += num_slots_;
    galois.push_back(context->param_.GetGaloisFactor(rotation));
  }
  auto primes = context->param_.GetPrimeVector(stage1_np);
  HostVector<word> h_primes(primes.begin(), primes.end());
  HostVector<word> inverses(primes.size());
  for (size_t i = 0; i < primes.size(); ++i) {
    inverses[i] = static_cast<word>(primeutil::InvModBase(primes[i]));
  }
  stage1_diagonal_pointers_.resize(pointers.size());
  stage1_galois_factors_.resize(galois.size());
  stage1_primes_.resize(primes.size());
  stage1_mont_inverses_.resize(primes.size());
  CopyHostToDevice(stage1_diagonal_pointers_, pointers);
  CopyHostToDevice(stage1_galois_factors_, galois);
  CopyHostToDevice(stage1_primes_, h_primes);
  CopyHostToDevice(stage1_mont_inverses_, inverses);

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

  Ct accum(input.GetNP());
  accum.SetScale(input.GetScale() * stage1_diagonals_.begin()->second.GetScale());
  accum.SetNumSlots(num_slots_);
  int degree = context->param_.degree_;
  int num_q = input.GetNP().GetNumQ();
  auto launch = [&](auto count) {
  SubringPrefixKernel<word, decltype(count)::value><<<DivCeil(num_q * degree, 256), 256>>>(
      accum.BxView().data(), accum.AxView().data(),
      input.BxConstView().data(), input.AxConstView().data(),
      stage1_diagonal_pointers_.data(), stage1_galois_factors_.data(),
      stage1_primes_.data(), stage1_mont_inverses_.data(),
      num_q, context->param_.log_degree_);
  };
  if (stage1_diagonals_.size() == 8) {
    launch(std::integral_constant<int, 8>{});
  } else {
    launch(std::integral_constant<int, 16>{});
  }

  context->Rescale(res, accum);
  res.SetNumSlots(num_slots_);
  AssertTrue(context->param_.NPToLevel(res.GetNP()) == start_level_ - 1,
             "MEBootCtS: Stage 1 output level mismatch");
}

template <typename word>
void MEBootCtS<word>::EvaluateStage2(Ct &res, const Ct &input,
                                     const EvkMap<word> &evk_map) const {
  EvaluateStage2(res, input, evk_map.GetAKSKeyMap());
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
                               bool min_ks) const {
  Evaluate(res, input, evk_map, evk_map.GetAKSKeyMap(), min_ks);
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
  context->SplitRealImag(ct_real, ct_imag, ct_in, conj_key);
}

template class MEBootCtS<uint32_t>;
template class MEBootCtS<uint64_t>;

}  // namespace cheddar
