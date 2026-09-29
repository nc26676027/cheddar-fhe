#pragma once

#include <string>
#include <unordered_map>

#include "core/Container.h"

namespace cheddar {

/**
 * @brief Container class for storing client-prepared Aggregated Key-Switching
 * (AKS) evaluation keys, separated from general rotation keys.
 *
 * @tparam word uint32_t or uint64_t
 */
template <typename word>
class AKSKeyMap : public std::unordered_map<int, EvaluationKey<word>> {
 private:
  using Base = std::unordered_map<int, EvaluationKey<word>>;
  using Evk = EvaluationKey<word>;

  const Evk &GetEvk(int key_idx) const;

 public:
  using Base::Base;
  AKSKeyMap(const AKSKeyMap &) = delete;
  AKSKeyMap &operator=(const AKSKeyMap &) = delete;
  AKSKeyMap(AKSKeyMap &&) = default;
  AKSKeyMap &operator=(AKSKeyMap &&) = default;

  /**
   * @brief Look up an AKS evaluation key for the given rotation index / diagonal offset.
   *
   * @param rot_idx rotation index / diagonal offset
   * @return const Evk& reference to the evaluation key
   */
  const Evk &GetKey(int rot_idx) const;

  /**
   * @brief Check whether an AKS evaluation key exists for the given rotation index.
   *
   * @param rot_idx rotation index / diagonal offset
   * @return true if key exists, false otherwise
   */
  bool HasKey(int rot_idx) const;
};

}  // namespace cheddar
