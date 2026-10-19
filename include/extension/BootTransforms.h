#pragma once

#include "extension/BootParameter.h"
#include "extension/BootstrapExecution.h"
#include "extension/LinearTransform.h"

namespace cheddar {

class MEBootExecution;
class MEBOOTDiscreteIdentity;

// Preparation and diagnostics for the frozen dense StC and CtS factors.
class BootTransforms {
 public:
  BootTransforms(ConstContextPtr<uint64_t> context,
                       const BootParameter &profile);
  void StC(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
           const EvkMap<uint64_t> &keys) const;
  void ScaleDown(Ciphertext<uint64_t> &res,
                 const Ciphertext<uint64_t> &input) const;
  void AddRequiredRotations(EvkRequest &req) const;
  const std::vector<LinearTransform<uint64_t>> &GetStC() const { return stc_; }
  const std::vector<LinearTransform<uint64_t>> &GetCtS() const { return cts_; }
  const std::vector<LinearTransform<uint64_t>> &GetBypass() const { return bypass_; }
  void CtS(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
           const EvkMap<uint64_t> &keys) const;
  void Bypass(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
              const EvkMap<uint64_t> &keys) const;
  // Fuse a caller's unit conversion and scale binding into the last factor.
  void BindBypassOutput(double gain, double scale);

 private:
  friend class MEBootExecution;
  friend class MEBOOTDiscreteIdentity;

  void Enter(Ciphertext<uint64_t> &proper, Ciphertext<uint64_t> &dense,
             const Ciphertext<uint64_t> &input, const EvkMap<uint64_t> &keys,
             const BootstrapExecution::StageMarker &mark = {},
             const BootstrapExecution::Observer &observe = {}) const;
  void Lift(Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input) const;
  void PrepareBypass(double gain, double scale);
  std::weak_ptr<const Context<uint64_t>> context_;
  BootParameter profile_;
  std::vector<LinearTransform<uint64_t>> stc_, cts_, bypass_;
  Constant<uint64_t> scale_down_, lift_multiplier_;
  double stc_output_scale_;
};

}  // namespace cheddar
