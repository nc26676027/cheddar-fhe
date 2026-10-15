#pragma once

#include "extension/BootParameter.h"
#include "extension/LinearTransform.h"

namespace cheddar {

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

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  BootParameter profile_;
  std::vector<LinearTransform<uint64_t>> stc_, cts_, bypass_;
  Constant<uint64_t> scale_down_;
  double stc_output_scale_;
};

}  // namespace cheddar
