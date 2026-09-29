#include "core/AKSKeyMap.h"

#include "common/Assert.h"

namespace cheddar {

template <typename word>
const EvaluationKey<word> &AKSKeyMap<word>::GetEvk(int key_idx) const {
  auto it = this->find(key_idx);
  AssertTrue(it != this->end(),
             "AKSKeyMap: Key not found for rotation index " + std::to_string(key_idx));
  return it->second;
}

template <typename word>
const EvaluationKey<word> &AKSKeyMap<word>::GetKey(int rot_idx) const {
  return GetEvk(rot_idx);
}

template <typename word>
bool AKSKeyMap<word>::HasKey(int rot_idx) const {
  return this->find(rot_idx) != this->end();
}

template class AKSKeyMap<uint32_t>;
template class AKSKeyMap<uint64_t>;

}  // namespace cheddar
