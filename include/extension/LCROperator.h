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
  LCROperator(ConstContextPtr<word> context,
              const StripedMatrix &matrix,
              int level,
              double scale);

  void Evaluate(Ciphertext<word> &res,
                const Ciphertext<word> &input,
                const AKSKeyMap<word> &aks_map) const;

  int GetLevel() const { return level_; }
  double GetScale() const { return scale_; }
  const std::vector<int> &GetSupports() const { return supports_; }

 private:
  ConstContextPtr<word> context_;
  int level_;
  double scale_;
  std::vector<int> supports_;
  std::map<int, Plaintext<word>> diagonals_;

  // Precomputed GPU constants for LCRProjectKernel
  DeviceVector<word> q_primes_gpu_;
  DeviceVector<word> inv_q_L_mod_qi_gpu_;
  word inv_q0_mod_q1_{0};
  int num_q_{0};
};

}  // namespace cheddar
