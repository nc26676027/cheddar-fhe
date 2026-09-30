#pragma once

#include <memory>
#include <vector>

#include "core/AKSKeyMap.h"
#include "core/Context.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"
#include "extension/BootParameter.h"
#include "extension/LCROperator.h"
#include "extension/LinearTransform.h"
#include "extension/MEBootMatrixPlanner.h"

namespace cheddar {

/**
 * @brief MEBootCtS coordinates the four-stage [4, 4, 3, 4] linear transformation
 * for CoeffsToSlots (CtS) on full-ring ciphertexts using Level-Conserving Rescaling
 * (LCR) and Aggregated Key-Switching (AKS) in Stage 1, and dense BSGS folding in
 * Stages 2 and 3, saving 1 modulus level.
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
   * @param dense_bases Dense BSGS bases for Stages 2 and 3 (default {64, 8})
   */
  MEBootCtS(ConstContextPtr<word> context,
            int num_slots,
            int start_level,
            double scaling_factor = 1.0,
            const std::vector<int> &widths = {4, 4, 3, 4},
            const std::vector<int> &dense_bases = {64, 8});

  MEBootCtS(ConstContextPtr<word> context,
            const BootParameter &boot_param,
            int num_slots,
            double scaling_factor = 1.0,
            const std::vector<int> &widths = {4, 4, 3, 4},
            const std::vector<int> &dense_bases = {64, 8});

  MEBootCtS(const MEBootCtS &) = delete;
  MEBootCtS &operator=(const MEBootCtS &) = delete;
  MEBootCtS(MEBootCtS &&) = default;
  MEBootCtS &operator=(MEBootCtS &&) = default;
  ~MEBootCtS() = default;

  /**
   * @brief Add all required Galois rotation keys for Stages 0, 2, and 3.
   */
  void AddRequiredRotations(EvkRequest &req, bool min_ks = false) const;

  /**
   * @brief Return the Stage 1 (LCR/AKS) matrix plan.
   */
  const StripedMatrix &GetStage1Matrix() const { return plans_[1].matrix; }

  /**
   * @brief Return the level at which Stage 1 (LCR/AKS) operates.
   */
  int GetStage1Level() const { return start_level_ - 1; }

  /**
   * @brief Return the LCR scale (q_L at stage 1 level).
   */
  double GetStage1LCRScale() const { return lcr_scale_; }

  /**
   * @brief Return the start level of CtS.
   */
  int GetStartLevel() const { return start_level_; }

  /**
   * @brief Return the output level of CtS (saving 1 modulus level, start_level - 3).
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

  /**
   * @brief Evaluate the 4-stage CtS linear transformation on input ciphertext.
   *
   * @param res Output ciphertext at level (start_level - 3)
   * @param input Input ciphertext at start_level
   * @param evk_map Evaluation key map containing Galois rotation keys
   * @param aks_map AKS key map containing Stage 1 AKS keys
   * @param min_ks Whether to use min_ks (default false)
   */
  void Evaluate(Ct &res, const Ct &input,
                const EvkMap<word> &evk_map,
                const AKSKeyMap<word> &aks_map,
                bool min_ks = false) const;

  void EvaluateStage0(Ct &res, const Ct &input,
                      const EvkMap<word> &evk_map,
                      bool min_ks = false) const;

  void EvaluateStage1(Ct &res, const Ct &input,
                      const AKSKeyMap<word> &aks_map) const;

  void EvaluateStage2(Ct &res, const Ct &input,
                      const EvkMap<word> &evk_map,
                      bool min_ks = false) const;

  void EvaluateStage3(Ct &res, const Ct &input,
                      const EvkMap<word> &evk_map,
                      bool min_ks = false) const;

  /**
   * @brief Split the complex ciphertext into real and imaginary ciphertexts.
   *
   * ct_real = ct_in + conj(ct_in)
   * ct_imag = (conj(ct_in) - ct_in) * i
   *
   * @param ct_real Output real ciphertext
   * @param ct_imag Output imaginary ciphertext
   * @param ct_in Input ciphertext
   * @param conj_key Conjugation evaluation key
   */
  void SplitRealImag(Ct &ct_real, Ct &ct_imag, const Ct &ct_in,
                     const Evk &conj_key) const;

 private:
  ConstContextPtr<word> context_;
  int num_slots_;
  int start_level_;
  double scaling_factor_;
  double lcr_scale_;

  std::vector<MEBootStagePlan> plans_;

  std::unique_ptr<LinearTransform<word>> stage0_;
  std::unique_ptr<LCROperator<word>> stage1_;
  std::unique_ptr<LinearTransform<word>> stage2_;
  std::unique_ptr<LinearTransform<word>> stage3_;

  void InitializeStages(const std::vector<int> &dense_bases);
};

}  // namespace cheddar
