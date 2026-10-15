#pragma once

#include <memory>
#include <vector>

#include "core/AKSKeyMap.h"
#include "core/Context.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"
#include "extension/LCROperator.h"
#include "extension/LinearTransform.h"
#include "extension/MEBootMatrixPlanner.h"

namespace cheddar {

/**
 * @brief MEBootCtS coordinates the four-stage [4, 4, 3, 4] linear transformation
 * for CoeffsToSlots (CtS) on full-ring ciphertexts using a Subring Prefix in
 * Stage 1, Level-Conserving Rescaling (LCR) and Aggregated Key-Switching (AKS)
 * in Stage 2, and dense BSGS folding in Stages 3 and 4. Bootstrap starts above
 * the key-switch ceiling: Stage 1 consumes only that temporary prefix, so
 * Stages 2-4 consume two normal-chain levels versus four for standard CtS.
 *
 * Input contract: full packing, Stage 1 width 4, and a sparse secret supported
 * on coefficient indices divisible by 16. Automorphisms preserve that secret.
 * The input must also satisfy LCROperator's centered small-coefficient bounds;
 * BootContext::ModUpToMax supplies this representation. Ordinary encryption
 * under the main secret is not a supported input to this Phase 1 evaluator.
 * All ciphertexts/NTTs retain full ring size; dimensional reduction is Phase 2.
 * See ADR-0004 for the accepted correction to the original BSGS specification.
 *
 * @tparam word uint32_t or uint64_t
 */
template <typename word>
class MEBootCtS {
 public:
  using Ct = Ciphertext<word>;
  using Pt = Plaintext<word>;
  using Evk = EvaluationKey<word>;
  using Complex = std::complex<double>;

  /**
   * @brief Construct a new MEBootCtS object.
   *
   * @param context ConstContextPtr to Cheddar context
   * @param num_slots Number of slots
   * @param start_level Modulus level where CtS begins
   * @param scaling_factor Overall scaling factor (default 1.0)
   * @param widths Stage widths (default {4, 4, 3, 4})
   * @param dense_bases Dense BSGS bases for Stages 3 and 4 (default {64, 8})
   */
  MEBootCtS(ConstContextPtr<word> context,
            int num_slots,
            int start_level,
            double scaling_factor = 1.0,
            const std::vector<int> &widths = {4, 4, 3, 4},
            const std::vector<int> &dense_bases = {64, 8},
            int subring_extension = 16);

  MEBootCtS(const MEBootCtS &) = delete;
  MEBootCtS &operator=(const MEBootCtS &) = delete;
  MEBootCtS(MEBootCtS &&) = default;
  MEBootCtS &operator=(MEBootCtS &&) = default;
  ~MEBootCtS() = default;

  /**
   * @brief Add all required Galois rotation keys for Stages 3 and 4.
   */
  void AddRequiredRotations(EvkRequest &req, bool min_ks = false) const;

  /**
   * @brief Return the Stage 2 (LCR/AKS) matrix plan.
   */
  const StripedMatrix &GetStage2Matrix() const { return plans_[1].matrix; }
  void BindAKSKeys(const AKSKeyMap<word> &keys) const { stage2_->BindKeys(keys); }

  /**
   * @brief Return the level at which Stage 2 (LCR/AKS) operates.
   */
  int GetStage2Level() const { return start_level_ - 1; }

  /**
   * @brief Return the LCR scale (q_L at Stage 2 level).
   */
  double GetStage2LCRScale() const { return lcr_scale_; }

  /**
   * @brief Return the start level of CtS.
   */
  int GetStartLevel() const { return start_level_; }

  /**
   * @brief Return the output level (start_level - 3, including the prefix).
   */
  int GetEndLevel() const { return start_level_ - 3; }

  /**
   * @brief Return the number of slots.
   */
  int GetNumSlots() const { return num_slots_; }

  /**
   * @brief Return the plans for all 4 stages.
   */
  const std::vector<MEBootStagePlan> &GetPlans() const { return plans_; }
  const std::map<int, Pt> &GetPrefixDiagonals() const { return stage1_diagonals_; }
  const std::map<int, Pt> &GetLCRDiagonals() const { return stage2_->GetDiagonals(); }

  /**
   * @brief Evaluate the 4-stage CtS linear transformation using the unified EvkMap.
   *
   * @param res Output ciphertext at level (start_level - 3)
   * @param input Input ciphertext at start_level
   * @param evk_map Evaluation key map containing Galois rotation and AKS keys
   * @param min_ks Whether to use min_ks (default false)
   */
  void Evaluate(Ct &res, const Ct &input,
                const EvkMap<word> &evk_map,
                bool min_ks = false) const;

  // Fused key-free gather/multiply/accumulate, followed by one Rescale.
  // Supports res aliasing input.
  void EvaluateStage1(Ct &res, const Ct &input) const;

  void EvaluateStage2(Ct &res, const Ct &input,
                      const EvkMap<word> &evk_map) const;

  void EvaluateStage2(Ct &res, const Ct &input,
                      const AKSKeyMap<word> &aks_map) const;

  void EvaluateStage3(Ct &res, const Ct &input,
                      const EvkMap<word> &evk_map,
                      bool min_ks = false) const;

  void EvaluateStage4(Ct &res, const Ct &input,
                      const EvkMap<word> &evk_map,
                      bool min_ks = false) const;

 private:
  ConstContextPtr<word> GetContext() const {
    auto ctx = context_.lock();
    AssertTrue(ctx != nullptr, "MEBootCtS: Context has been destroyed");
    return ctx;
  }

  std::weak_ptr<const Context<word>> context_;
  int num_slots_;
  int start_level_;
  double scaling_factor_;
  double lcr_scale_;

  std::vector<MEBootStagePlan> plans_;

  std::map<int, Plaintext<word>> stage1_diagonals_;
  DeviceVector<uint64_t> stage1_diagonal_pointers_;
  DeviceVector<word> stage1_galois_factors_;
  DeviceVector<word> stage1_primes_;
  DeviceVector<word> stage1_mont_inverses_;
  std::unique_ptr<LCROperator<word>> stage2_;
  std::unique_ptr<LinearTransform<word>> stage3_;
  std::unique_ptr<LinearTransform<word>> stage4_;

  void InitializeStages(const std::vector<int> &dense_bases);
};

}  // namespace cheddar
