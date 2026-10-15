#pragma once

#include <cmath>
#include "core/EvkMap.h"
#include "core/EvkRequest.h"
#include "extension/DiscreteIntegerCleaner.h"
#include "extension/LinearTransform.h"

namespace cheddar {

// StC-first original Discrete CKKS with conventional full-ring sparse entry.
// This research implementation does not alter BootContext::Boot dispatch.
class DiscreteIdentityBaseline {
 public:
  using Ct = Ciphertext<uint64_t>;
  using Observer = std::function<void(const char *, const Ct &)>;
  DiscreteIdentityBaseline(
      ConstContextPtr<uint64_t> context,
      const std::vector<std::vector<double>> &extractor_coefficients);
  void AddRequiredRotations(EvkRequest &request) const;
  void Evaluate(Ct &result, const Ct &input, const EvkMap<uint64_t> &keys,
                const Observer &observe = {}) const;
  int OutputLevel() const { return cleaner_->OutputLevel() - 1; }
  double OutputScale() const { return std::exp2(40); }
  double EffectiveGap() const { return gap_; }

 private:
  using Transform = LinearTransform<uint64_t>;
  void Lift(Ct &result, const Ct &input) const;
  std::weak_ptr<const Context<uint64_t>> context_;
  std::vector<Transform> stc_, low_cts_, high_cts_;
  std::unique_ptr<DiscreteIntegerCleaner> cleaner_;
  Constant<uint64_t> normalize_;
  double gap_ = 0, integer_scale_ = 0;
};

}  // namespace cheddar
