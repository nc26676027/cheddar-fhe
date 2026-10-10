#include "core/EvkMap.h"

#include "common/Assert.h"
#include "common/CommonUtils.h"

namespace cheddar {

template <typename word>
EvaluationKey<word> &EvkMap<word>::EmplaceRotationKey(int rot_idx,
                                                      const NPInfo &np,
                                                      int beta) {
  AssertTrue(rot_idx > 0, "EmplaceRotationKey: Invalid rotation index");
  auto it = rotation_keys_.find(rot_idx);
  if (it != rotation_keys_.end()) {
    Warn("Overwriting the rotation evk for index " + std::to_string(rot_idx));
    rotation_keys_.erase(it);
  }
  auto [ins_it, _] = rotation_keys_.try_emplace(rot_idx, np, beta);
  return ins_it->second;
}

template <typename word>
EvaluationKey<word> &EvkMap<word>::EmplaceMultiplicationKey(const NPInfo &np,
                                                            int beta) {
  if (mult_key_.has_value()) {
    Warn("Overwriting the multiplication evk");
  }
  mult_key_.emplace(np, beta);
  return *mult_key_;
}

template <typename word>
EvaluationKey<word> &EvkMap<word>::EmplaceConjugationKey(const NPInfo &np,
                                                         int beta) {
  if (conj_key_.has_value()) {
    Warn("Overwriting the conjugation evk");
  }
  conj_key_.emplace(np, beta);
  return *conj_key_;
}

template <typename word>
EvaluationKey<word> &EvkMap<word>::EmplaceDenseToSparseKey(const NPInfo &np,
                                                           int beta) {
  if (dts_key_.has_value()) {
    Warn("Overwriting the dense-to-sparse evk");
  }
  dts_key_.emplace(np, beta);
  return *dts_key_;
}

template <typename word>
EvaluationKey<word> &EvkMap<word>::EmplaceSparseToDenseKey(const NPInfo &np,
                                                           int beta) {
  if (std_key_.has_value()) {
    Warn("Overwriting the sparse-to-dense evk");
  }
  std_key_.emplace(np, beta);
  return *std_key_;
}

template <typename word>
EvaluationKey<word> &EvkMap<word>::EmplaceAKSKey(int rot_idx, const NPInfo &np,
                                                 int beta) {
  return aks_keys_.EmplaceKey(rot_idx, np, beta);
}

template <typename word>
bool EvkMap<word>::HasRotationKey(int rot_idx) const {
  return rotation_keys_.find(rot_idx) != rotation_keys_.end();
}

template <typename word>
bool EvkMap<word>::HasMultiplicationKey() const {
  return mult_key_.has_value();
}

template <typename word>
bool EvkMap<word>::HasConjugationKey() const {
  return conj_key_.has_value();
}

template <typename word>
bool EvkMap<word>::HasDenseToSparseKey() const {
  return dts_key_.has_value();
}

template <typename word>
bool EvkMap<word>::HasSparseToDenseKey() const {
  return std_key_.has_value();
}

template <typename word>
bool EvkMap<word>::HasAKSKey(int rot_idx) const {
  return aks_keys_.HasKey(rot_idx);
}

template <typename word>
const EvaluationKey<word> &EvkMap<word>::GetRotationKey(int rot_idx) const {
  AssertTrue(rot_idx > 0, "GetRotationKey: Invalid rotation index");
  auto it = rotation_keys_.find(rot_idx);
  AssertTrue(it != rotation_keys_.end(),
             "GetRotationKey: Key not found for index " +
                 std::to_string(rot_idx));
  return it->second;
}

template <typename word>
const EvaluationKey<word> &EvkMap<word>::GetMultiplicationKey() const {
  AssertTrue(mult_key_.has_value(), "GetMultiplicationKey: Key not found");
  return *mult_key_;
}

template <typename word>
const EvaluationKey<word> &EvkMap<word>::GetConjugationKey() const {
  AssertTrue(conj_key_.has_value(), "GetConjugationKey: Key not found");
  return *conj_key_;
}

template <typename word>
const EvaluationKey<word> &EvkMap<word>::GetDenseToSparseKey() const {
  AssertTrue(dts_key_.has_value(), "GetDenseToSparseKey: Key not found");
  return *dts_key_;
}

template <typename word>
const EvaluationKey<word> &EvkMap<word>::GetSparseToDenseKey() const {
  AssertTrue(std_key_.has_value(), "GetSparseToDenseKey: Key not found");
  return *std_key_;
}

template <typename word>
const EvaluationKey<word> &EvkMap<word>::GetAKSKey(int rot_idx) const {
  return aks_keys_.GetKey(rot_idx);
}

template class EvkMap<uint32_t>;
template class EvkMap<uint64_t>;

}  // namespace cheddar
