#include "core/AKSKeyMap.h"

#include "common/Assert.h"

namespace cheddar {

template <typename word>
EvaluationKey<word> &AKSKeyMap<word>::EmplaceKey(int rot_idx, const NPInfo &np,
                                                 int beta) {
  auto it = keys_.find(rot_idx);
  if (it != keys_.end()) {
    keys_.erase(it);
  }
  auto [ins_it, _] = keys_.try_emplace(rot_idx, np, beta);
  return ins_it->second;
}

template <typename word>
const EvaluationKey<word> &AKSKeyMap<word>::GetKey(int rot_idx) const {
  auto it = keys_.find(rot_idx);
  AssertTrue(it != keys_.end(),
             "AKSKeyMap: Key not found for rotation index " +
                 std::to_string(rot_idx));
  return it->second;
}

template <typename word>
bool AKSKeyMap<word>::HasKey(int rot_idx) const {
  return keys_.find(rot_idx) != keys_.end();
}

template class AKSKeyMap<uint32_t>;
template class AKSKeyMap<uint64_t>;

}  // namespace cheddar
