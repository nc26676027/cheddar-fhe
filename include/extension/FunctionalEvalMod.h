#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "core/Context.h"
#include "extension/BootParameter.h"
#include "extension/EvalPoly.h"
#include "extension/Func15RBootCoefficients.inc"

namespace cheddar {

enum class FunctionalMode {
  kActivation,  // ReLU-like activation: 2 * odd + even + 0.0625*ratio + output_shift
  kIdentity     // Identity refresh: 4 * odd
};

/**
 * @brief GPU RBOOT Functional EvalMod for LogN=15 (Func15).
 * Evaluates the composite minimax polynomial (degree 62 Chebyshev + DA2 + degree 15 tails)
 * from Level 16 (Scale 2^43) down to Level 3 (Scale 2^43).
 */
class FunctionalEvalMod {
 public:
  FunctionalEvalMod(ConstContextPtr<uint64_t> context,
                    const BootParameter &profile);

  void Evaluate(Ciphertext<uint64_t> &res,
                const Ciphertext<uint64_t> &input,
                const EvaluationKey<uint64_t> &mult_key,
                FunctionalMode mode = FunctionalMode::kActivation,
                const std::function<void(const char *, const Ciphertext<uint64_t> &)> &observe = {}) const;

  // Exact scalar reference evaluation for validation
  double PlainEvaluate(double normalized_input,
                       FunctionalMode mode = FunctionalMode::kActivation) const;

  int InputLevel() const { return profile_.mod1_level; }
  int OutputLevel() const { return profile_.OutputLevel(); }
  double OutputScale() const { return profile_.application_scale; }

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  BootParameter profile_;

  double polynomial_scale_;
  EvalPoly<uint64_t> main_chebyshev_;
  std::vector<AXYPBZ<uint64_t>> double_angle_;
  std::unique_ptr<AXYPBZ<uint64_t>> da_even_step_;

  EvalPoly<uint64_t> odd_tail_;
  EvalPoly<uint64_t> even_tail_;

  Constant<uint64_t> offset_const_;
  Constant<uint64_t> cosine_lift_const_;
  Constant<uint64_t> phase_lift_const_;
  Constant<uint64_t> tail_lift_const_;
  Constant<uint64_t> const_0_0625_;
  Constant<uint64_t> shift_const_;
};

}  // namespace cheddar
