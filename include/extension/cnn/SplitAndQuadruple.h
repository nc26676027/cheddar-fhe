#pragma once

#include <memory>

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
 * @brief SplitAndQuadruple remaps a refreshed tensor (Level 3, scale 2^43) on
 * the `pre` layout onto the 2x2-quadrant packing of the halved grid: each
 * input channel c becomes four channels 4c + (row parity * 2 + col parity).
 * Uses one transform on the ciphertext and one on its conjugate (which also
 * performs the real-in-complex extraction), consuming one level.
 * Output: Level 2, scale 2^43, ready for a mask-free stride-2 ConvStC.
 */
class SplitAndQuadruple {
 public:
  SplitAndQuadruple(ConstContextPtr<uint64_t> context,
                    const TensorShape &pre_shape, const BootParameter &profile);

  const TensorShape &GetPreShape() const { return pre_; }
  const TensorShape &GetPostShape() const { return post_; }

  /** Plaintext diagonal matrices (index 0: ciphertext, 1: conjugate). */
  static StripedMatrix BuildMatrix(const TensorShape &pre,
                                   const TensorShape &post, int slots,
                                   bool conjugate_part);

  void AddRequiredRotations(EvkRequest &req) const;

  void Evaluate(Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &in,
                const EvkMap<uint64_t> &keys) const;

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  TensorShape pre_;
  TensorShape post_;
  BootParameter profile_;
  std::unique_ptr<LinearTransform<uint64_t>> direct_;
  std::unique_ptr<LinearTransform<uint64_t>> conjugate_;
};

}  // namespace cnn
}  // namespace cheddar
