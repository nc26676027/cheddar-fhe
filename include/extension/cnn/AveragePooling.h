#pragma once

#include <memory>
#include <vector>

#include "core/Context.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"
#include "extension/BootParameter.h"
#include "extension/LinearTransform.h"
#include "extension/StripedMatrix.h"
#include "extension/cnn/TensorShape.h"

namespace cheddar {
namespace cnn {

/**
 * @brief AveragePooling evaluates global average pooling on the 8x8 x 64 channels tensor.
 * Evaluates the offline fused polyphase cascade using two diagonal linear transforms
 * (real and imaginary components), mapping Level 3 (scale 2^43) to Level 2 (scale 2^43).
 * Output places channel c's average at slot c * 64.
 */
class AveragePooling {
 public:
  AveragePooling(ConstContextPtr<uint64_t> context,
                 const BootParameter &profile);

  void AddRequiredRotations(EvkRequest &req) const;

  void Pool(Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &in,
            const EvkMap<uint64_t> &keys) const;

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  BootParameter profile_;
  std::unique_ptr<LinearTransform<uint64_t>> real_transform_;
  std::unique_ptr<LinearTransform<uint64_t>> imag_transform_;

  ConstContextPtr<uint64_t> GetContext() const {
    auto ctx = context_.lock();
    AssertTrue(ctx != nullptr, "Context has been destroyed");
    return ctx;
  }
};

}  // namespace cnn
}  // namespace cheddar
