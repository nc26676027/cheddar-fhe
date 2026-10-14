#pragma once

#include <functional>

#include "extension/EvalPoly.h"
#include "extension/Identity16Profile.h"

namespace cheddar {

// Native selected CosDiscrete evaluation. The input's residues carry the
// CtS-normalized units at 2^48; normalization to 2^58 divides values by 1024.
class Identity16Mod1 {
 public:
  Identity16Mod1(ConstContextPtr<uint64_t> context,
                const Identity16Profile &profile);
  void Evaluate(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
                const EvaluationKey<uint64_t> &key,
                const std::function<void(int, const Ciphertext<uint64_t> &)> &observe = {}) const;
  double PlainEvaluate(double normalized_input) const;
  double PlainPolynomial(double normalized_input) const {
    return polynomial_.PlainEvaluate(normalized_input - 0.25 / profile_.k);
  }
  int GetDegree() const { return polynomial_.GetPolyDegree(); }
  double GetPolynomialScale() const { return polynomial_scale_; }

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  Identity16Profile profile_;
  double polynomial_scale_;
  EvalPoly<uint64_t> polynomial_;
  Constant<uint64_t> offset_, lift_;
  std::vector<AXYPBZ<uint64_t>> double_angle_;
};

}  // namespace cheddar
