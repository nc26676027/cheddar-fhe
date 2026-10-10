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
class AKSKeyMap {
 private:
  using Evk = EvaluationKey<word>;
  std::unordered_map<int, Evk> keys_;

 public:
  AKSKeyMap() = default;
  AKSKeyMap(const AKSKeyMap &) = delete;
  AKSKeyMap &operator=(const AKSKeyMap &) = delete;
  AKSKeyMap(AKSKeyMap &&) = default;
  AKSKeyMap &operator=(AKSKeyMap &&) = default;

  /**
   * @brief Create or overwrite an AKS evaluation key for the given rotation index.
   *
   * @param rot_idx rotation index / diagonal offset
   * @param np NPInfo for the evaluation key
   * @param beta gadget decomposition count (default 1)
   * @return Evk& mutable reference to the inserted evaluation key
   */
  Evk &EmplaceKey(int rot_idx, const NPInfo &np, int beta = 1);

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

  size_t Size() const { return keys_.size(); }
  size_t size() const { return keys_.size(); }
  bool Empty() const { return keys_.empty(); }
  bool empty() const { return keys_.empty(); }
  void Clear() { keys_.clear(); }
};

}  // namespace cheddar
