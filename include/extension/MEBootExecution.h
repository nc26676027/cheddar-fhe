#pragma once

#include <functional>
#include "extension/MEBootEvalMod.h"
#include "extension/BootTransforms.h"
#include "extension/MEBootCtS.h"

namespace cheddar {

class MEBootExecution {
 public:
  MEBootExecution(ConstContextPtr<uint64_t> context, const BootParameter &profile);
  void AddRequiredRotations(EvkRequest &req) const;
  const MEBootCtS<uint64_t> &GetMEBootCtS() const;
  void Evaluate(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
                const EvkMap<uint64_t> &keys,
                const std::function<void(const char *)> &mark = {},
                const std::function<void(const char *, const Ciphertext<uint64_t> &)> &observe = {}) const;
  const BootTransforms &GetTransforms() const { return transforms_; }
  const BootParameter &GetProfile() const { return profile_; }
  double PlainMod1(double value) const { return mod1_.PlainEvaluate(value); }
  double GetLCRInputBound() const { return lcr_input_bound_; }
  double GetLCRProjectionBound() const { return lcr_projection_bound_; }
  void VerifyEncodedLCRBounds();

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  BootParameter profile_;
  BootTransforms transforms_;
  MEBootEvalMod mod1_;
  std::unique_ptr<MEBootCtS<uint64_t>> meboot_cts_;
  Constant<uint64_t> modup_multiplier_, correction_;
  double lcr_input_bound_ = 0, lcr_projection_bound_ = 0;
  bool encoded_lcr_bound_verified_ = false;
  void Lift(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
            int level) const;
};

}  // namespace cheddar
