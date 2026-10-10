#pragma once

#include <optional>
#include <unordered_map>

#include "core/AKSKeyMap.h"
#include "core/Container.h"

namespace cheddar {

/**
 * @brief Encapsulated store for client-prepared evaluation keys (rotation,
 * multiplication, conjugation, secret-switching, and AKS keys).
 *
 * @tparam word uint32_t or uint64_t
 */
template <typename word>
class EvkMap {
 private:
  using Evk = EvaluationKey<word>;

  std::unordered_map<int, Evk> rotation_keys_;
  std::optional<Evk> mult_key_;
  std::optional<Evk> conj_key_;
  std::optional<Evk> dts_key_;
  std::optional<Evk> std_key_;
  AKSKeyMap<word> aks_keys_;

 public:
  EvkMap() = default;
  EvkMap(const EvkMap &) = delete;
  EvkMap &operator=(const EvkMap &) = delete;
  EvkMap(EvkMap &&) = default;
  EvkMap &operator=(EvkMap &&) = default;

  // Typed insertion / overwrite
  Evk &EmplaceRotationKey(int rot_idx, const NPInfo &np, int beta);
  Evk &EmplaceMultiplicationKey(const NPInfo &np, int beta);
  Evk &EmplaceConjugationKey(const NPInfo &np, int beta);
  Evk &EmplaceDenseToSparseKey(const NPInfo &np, int beta);
  Evk &EmplaceSparseToDenseKey(const NPInfo &np, int beta);
  Evk &EmplaceAKSKey(int rot_idx, const NPInfo &np, int beta = 1);

  // Typed queries
  bool HasRotationKey(int rot_idx) const;
  bool HasMultiplicationKey() const;
  bool HasConjugationKey() const;
  bool HasDenseToSparseKey() const;
  bool HasSparseToDenseKey() const;
  bool HasAKSKey(int rot_idx) const;

  // Typed lookups
  const Evk &GetRotationKey(int rot_idx) const;
  const Evk &GetMultiplicationKey() const;
  const Evk &GetConjugationKey() const;
  const Evk &GetDenseToSparseKey() const;
  const Evk &GetSparseToDenseKey() const;
  const Evk &GetAKSKey(int rot_idx) const;

  const AKSKeyMap<word> &GetAKSKeyMap() const { return aks_keys_; }
  AKSKeyMap<word> &GetMutableAKSKeyMap() { return aks_keys_; }
};

}  // namespace cheddar
