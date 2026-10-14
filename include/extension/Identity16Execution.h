#pragma once

#include <functional>
#include "extension/Identity16Mod1.h"
#include "extension/Identity16Transforms.h"
#include "extension/MEBootCtS.h"

namespace cheddar {

class Identity16Execution {
 public:
  Identity16Execution(ConstContextPtr<uint64_t> context, Identity16Route route);
  void AddRequiredRotations(EvkRequest &req) const;
  const MEBootCtS<uint64_t> &GetJointCtS() const;
  void Evaluate(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
                const EvkMap<uint64_t> &keys,
                const std::function<void(const char *)> &mark = {},
                const std::function<void(const char *, const Ciphertext<uint64_t> &)> &observe = {}) const;
  const Identity16Transforms &GetTransforms() const { return transforms_; }
  const Identity16Profile &GetProfile() const { return profile_; }
  double PlainMod1(double value) const { return mod1_.PlainEvaluate(value); }
  double GetLCRInputBound() const { return lcr_input_bound_; }
  double GetLCRProjectionBound() const { return lcr_projection_bound_; }
  void VerifyEncodedLCRBounds();

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  Identity16Profile profile_;
  Identity16Transforms transforms_;
  Identity16Mod1 mod1_;
  std::unique_ptr<MEBootCtS<uint64_t>> joint_;
  Constant<uint64_t> modup_multiplier_, correction_;
  double lcr_input_bound_ = 0, lcr_projection_bound_ = 0;
  bool encoded_lcr_bound_verified_ = false;
  void Lift(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
            int level) const;
};

}  // namespace cheddar
