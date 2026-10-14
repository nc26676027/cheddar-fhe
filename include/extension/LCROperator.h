#pragma once

#include <map>
#include <vector>

#include "common/CommonUtils.h"
#include "core/AKSKeyMap.h"
#include "core/Container.h"
#include "core/Context.h"
#include "extension/MEBootMatrixPlanner.h"

namespace cheddar {

template <typename word>
class LCROperator {
 public:
  // The single-digit AKS path requires centered small-coefficient inputs.
  // c1 and the c0-branch quotient must fit the centered CRT range reconstructed
  // from the first three Q limbs (uint32_t) or two Q limbs (uint64_t), using
  // fewer limbs when unavailable. Uniform full-Q ciphertext components from
  // ordinary key switching do not satisfy this contract. See ADR-0004.
  LCROperator(ConstContextPtr<word> context,
              const StripedMatrix &matrix,
              int level,
              double scale);

  void Evaluate(Ciphertext<word> &res,
                const Ciphertext<word> &input,
                const AKSKeyMap<word> &aks_map) const;
  void BindKeys(const AKSKeyMap<word> &aks_map) const;

  int GetLevel() const { return level_; }
  double GetScale() const { return scale_; }
  const std::vector<int> &GetSupports() const { return supports_; }
  const std::map<int, Plaintext<word>> &GetDiagonals() const { return diagonals_; }

 private:
  ConstContextPtr<word> GetContext() const {
    auto ctx = context_.lock();
    AssertTrue(ctx != nullptr, "LCROperator: Context has been destroyed");
    return ctx;
  }

  std::weak_ptr<const Context<word>> context_;
  int level_;
  double scale_;
  std::vector<int> supports_;
  std::map<int, Plaintext<word>> diagonals_;

  // Precomputed GPU constants for LCRProjectKernel and CenteredSmallPolyLiftKernel
  DeviceVector<word> q_primes_gpu_;
  DeviceVector<word> inv_q_L_mont_gpu_;
  DeviceVector<word> q_mont_inv_gpu_;
  DeviceVector<word> p_primes_gpu_;
  DeviceVector<word> p_mont_R2_gpu_;
  DeviceVector<word> p_mont_inv_gpu_;
  DeviceVector<word> qp_primes_gpu_;
  DeviceVector<word> qp_mont_inv_gpu_;
  DeviceVector<word> galois_gpu_;
  mutable DeviceVector<uint64_t> bound_pointers_;
  mutable std::vector<uint64_t> bound_key_addresses_;
  word inv_q0_mod_q1_{0};
  word inv_q0q1_mod_q2_{0};
  int num_q_{0};
  int alpha_{0};
};

}  // namespace cheddar
