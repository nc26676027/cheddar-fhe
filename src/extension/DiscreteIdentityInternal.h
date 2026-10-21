#pragma once

#include <memory>
#include <vector>

#include "common/Assert.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"
#include "extension/BootTransforms.h"
#include "extension/BootstrapExecution.h"
#include "extension/DiscreteIdentityBootstrap.h"
#include "extension/DiscreteIntegerCleaner.h"
#include "extension/MEBootCtS.h"

namespace cheddar {

// Internal execution engine for Discrete EvalRound: digit cleaning, bypass alignment,
// and subtractive return.
class DiscreteIdentityExecution {
 public:
  using Ct = Ciphertext<uint64_t>;
  using Observer = std::function<void(const char *, const Ct &)>;
  using ChannelAligner = std::function<void(Ct &)>;

  DiscreteIdentityExecution(
      ConstContextPtr<uint64_t> context,
      std::unique_ptr<DiscreteIntegerCleaner> cleaner,
      Constant<uint64_t> bypass_alignment,
      Constant<uint64_t> normalize, double gap, int output_level,
      double output_scale);
  DiscreteIdentityExecution(ConstContextPtr<uint64_t> context,
                            const BootParameter &profile,
                            const DiscreteIdentityParameters &parameters,
                            BootTransforms &transforms);
  void CleanInteger(Ct &integer, Ct &&low,
                    const EvkMap<uint64_t> &keys,
                    const Observer &observe = {}) const;
  void CleanInteger(Ct &integer, Ct &&low,
                    const EvkMap<uint64_t> &keys,
                    const ChannelAligner &align,
                    const Observer &observe = {}) const;
  void CleanInteger(Ct &integer, const Ct &low,
                    const EvkMap<uint64_t> &keys,
                    const Observer &observe = {}) const;
  void CleanInteger(Ct &integer, const Ct &low,
                    const EvkMap<uint64_t> &keys,
                    const ChannelAligner &align,
                    const Observer &observe = {}) const;
  void AlignBypass(Ct &high, const Ct &bypass) const;
  void Finish(Ct &result, const Ct &high, const Ct &integer,
              const Observer &observe = {}) const;
  int OutputLevel() const { return output_level_; }
  double OutputScale() const { return output_scale_; }
  double EffectiveGap() const { return gap_; }

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  std::unique_ptr<DiscreteIntegerCleaner> cleaner_;
  Constant<uint64_t> bypass_alignment_, cleaner_alignment_, cleaner_offset_;
  Constant<uint64_t> normalize_;
  double gap_ = 0;
  int output_level_ = 0;
  double output_scale_ = 0;
  int cleaner_alignment_level_ = 0;
  double cleaner_input_scale_ = 0;
  bool cleaner_alignment_rescale_ = true;
};

// StC-first original Discrete CKKS with conventional full-ring sparse entry.
class DiscreteIdentityBaseline {
 public:
  using Ct = Ciphertext<uint64_t>;
  using Observer = std::function<void(const char *, const Ct &)>;
  DiscreteIdentityBaseline(
      ConstContextPtr<uint64_t> context,
      const std::vector<std::vector<double>> &extractor_coefficients);
  DiscreteIdentityBaseline(ConstContextPtr<uint64_t> context,
                           const DiscreteIdentityParameters &parameters);
  void AddRequiredRotations(EvkRequest &request) const;
  void Evaluate(Ct &result, const Ct &input, const EvkMap<uint64_t> &keys,
                const Observer &observe = {}) const;
  DiscreteIdentityTiming Profile(
      Ct &result, const Ct &input, const EvkMap<uint64_t> &keys) const;
  int OutputLevel() const { return execution_->OutputLevel(); }
  double OutputScale() const { return execution_->OutputScale(); }
  double EffectiveGap() const { return execution_->EffectiveGap(); }

 private:
  using Transform = LinearTransform<uint64_t>;
  void Lift(Ct &result, const Ct &input) const;
  std::weak_ptr<const Context<uint64_t>> context_;
  std::vector<Transform> stc_, low_cts_, high_cts_;
  std::unique_ptr<DiscreteIdentityExecution> execution_;
  double integer_scale_ = 0;
};

// Internal Discrete EvalRound adapter sharing MEBOOT's entry, four-role CtS,
// and the common identity execution module. BootContext owns its production dispatch.
class MEBOOTDiscreteIdentity : public BootstrapExecution {
 public:
  using Ct = Ciphertext<uint64_t>;
  using Observer = std::function<void(const char *, const Ct &)>;
  MEBOOTDiscreteIdentity(
      ConstContextPtr<uint64_t> context, const BootParameter &profile,
      const std::vector<std::vector<double>> &extractor_coefficients);
  MEBOOTDiscreteIdentity(ConstContextPtr<uint64_t> context,
                         const BootParameter &profile,
                         const DiscreteIdentityParameters &parameters);
  void AddRequiredRotations(EvkRequest &request) const override;
  const MEBootCtS<uint64_t> &GetMEBootCtS() const override {
    AssertTrue(low_cts_ != nullptr, "Discrete MEBOOT CtS is not prepared");
    return *low_cts_;
  }
  void Evaluate(Ct &result, const Ct &input, const EvkMap<uint64_t> &keys,
                const StageMarker &mark = {},
                const Observer &observe = {}) const override;
  void Evaluate(Ct &result, const Ct &input, const EvkMap<uint64_t> &keys,
                const Observer &observe) const {
    Evaluate(result, input, keys, {}, observe);
  }
  DiscreteIdentityTiming Profile(
      Ct &result, const Ct &input, const EvkMap<uint64_t> &keys) const;
  int OutputLevel() const override { return execution_->OutputLevel(); }
  double OutputScale() const override { return execution_->OutputScale(); }
  double EffectiveGap() const { return execution_->EffectiveGap(); }

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  BootTransforms transforms_;
  std::unique_ptr<MEBootCtS<uint64_t>> low_cts_;
  std::unique_ptr<DiscreteIdentityExecution> execution_;
};

}  // namespace cheddar
