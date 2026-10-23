#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "extension/BootstrapExecution.h"
#include "extension/BootParameter.h"
#include "extension/FunctionalEvalMod.h"
#include "extension/LinearTransform.h"
#include "extension/MEBootCtS.h"

namespace cheddar {

/**
 * @brief Func15 RBOOT Bootstrap Route Execution.
 * Orchestrates the full-slot (16,384 slots) LogN=15 bootstrapping pipeline:
 * Level 2 (Scale 2^43) -> StC [7, 7] -> Level 0 -> Entry 4-bit (beta=9) + Lift
 * -> Level 19 -> Subring CtS [3, 3, 4, 4] -> Level 16 -> FunctionalEvalMod
 * -> Level 3 (Scale 2^43).
 */
class Func15RBootExecution : public BootstrapExecution {
 public:
  Func15RBootExecution(ConstContextPtr<uint64_t> context,
                       const BootParameter &profile);

  void AddRequiredRotations(EvkRequest &req) const override;
  const MEBootCtS<uint64_t> &GetMEBootCtS() const override;

  void Evaluate(Ciphertext<uint64_t> &res,
                const Ciphertext<uint64_t> &input,
                const EvkMap<uint64_t> &keys,
                const StageMarker &mark = {},
                const Observer &observe = {}) const override;

  void Evaluate(Ciphertext<uint64_t> &res,
                const Ciphertext<uint64_t> &input,
                const EvkMap<uint64_t> &keys,
                FunctionalMode mode,
                bool real_only = true,
                const StageMarker &mark = {},
                const Observer &observe = {}) const;

  /**
   * @brief Continues the route from a Level 0, scale 2^31 coefficient-domain
   * ciphertext (e.g. a fused Conv-StC output), skipping the internal StC.
   * Coefficient value 8u refreshes to f(u) at Level 3, scale 2^43.
   */
  void EvaluatePostStC(Ciphertext<uint64_t> &res,
                       const Ciphertext<uint64_t> &stc_output,
                       const EvkMap<uint64_t> &keys, FunctionalMode mode,
                       bool real_only, const StageMarker &mark = {},
                       const Observer &observe = {}) const;

  int OutputLevel() const override { return profile_.OutputLevel(); }
  double OutputScale() const override { return profile_.application_scale; }

  const FunctionalEvalMod &GetEvalMod() const { return evalmod_; }
  const std::vector<LinearTransform<uint64_t>> &GetStC() const { return stc_; }

  void StC(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
           const EvkMap<uint64_t> &keys) const;
  void ScaleDown(Ciphertext<uint64_t> &res,
                 const Ciphertext<uint64_t> &input) const;
  void Lift(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input) const;

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  BootParameter profile_;
  std::vector<LinearTransform<uint64_t>> stc_;
  Constant<uint64_t> scale_down_;
  Constant<uint64_t> lift_multiplier_;
  double stc_output_scale_;
  std::unique_ptr<MEBootCtS<uint64_t>> meboot_cts_;
  FunctionalEvalMod evalmod_;
};

}  // namespace cheddar
