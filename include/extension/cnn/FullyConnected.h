#pragma once

#include <memory>
#include <vector>

#include "core/Context.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"
#include "extension/BootParameter.h"
#include "extension/LinearTransform.h"
#include "extension/StripedMatrix.h"

namespace cheddar {
namespace cnn {

/**
 * @brief FullyConnected evaluates the 10-class linear classification head
 * on the pooled 64-feature vector.
 * Input: pooled ciphertext at Level 2, scale 2^43.
 * Output: logits ciphertext at Level 1, scale 2^43, with logits in slots 0 .. 9.
 */
class FullyConnected {
 public:
  FullyConnected(ConstContextPtr<uint64_t> context,
                 const BootParameter &profile,
                 const std::vector<double> &weights,
                 const std::vector<double> &bias);

  void AddRequiredRotations(EvkRequest &req) const;

  void Classify(Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &pooled,
                const EvkMap<uint64_t> &keys) const;

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  BootParameter profile_;
  std::unique_ptr<LinearTransform<uint64_t>> fc_transform_;
  Plaintext<uint64_t> bias_pt_;

  ConstContextPtr<uint64_t> GetContext() const {
    auto ctx = context_.lock();
    AssertTrue(ctx != nullptr, "Context has been destroyed");
    return ctx;
  }
};

}  // namespace cnn
}  // namespace cheddar
