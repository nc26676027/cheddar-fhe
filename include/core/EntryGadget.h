#pragma once

#include "core/Container.h"
#include "core/NTT.h"

namespace cheddar {

// Experiment-only base-16 DtS gadget. Its Q0/P tables own a separate basis.
template <typename word>
class EntryGadget {
 public:
  static constexpr int kDigitBits = 4;
  explicit EntryGadget(const Parameter<word>& normal);
  int GetDigitCount() const { return digits_; }
  void DecomposeDigit(DeviceVector<word>& result,
                      const DeviceVector<word>& coefficients, int index) const;
  void PopulateKey(EvaluationKey<word>& key,
                   const DvConstView<word>& dense_q0,
                   const DvConstView<word>& sparse_q0, double sigma) const;
  void Switch(Ciphertext<word>& result, const Ciphertext<word>& input,
              const EvaluationKey<word>& key) const;

 private:
  Parameter<word> param_;
  NTTHandler<word> ntt_;
  int digits_;
};

}  // namespace cheddar
