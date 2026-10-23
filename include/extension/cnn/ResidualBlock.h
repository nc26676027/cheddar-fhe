#pragma once

#include <memory>
#include <string>

#include "core/Context.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"
#include "extension/BootParameter.h"
#include "extension/Func15RBootExecution.h"
#include "extension/cnn/ConvStCFusion.h"
#include "extension/cnn/SplitAndQuadruple.h"
#include "extension/cnn/TensorShape.h"

namespace cheddar {
namespace cnn {

/**
 * @brief Deep homomorphic residual block module.
 *
 * Encapsulates:
 * - Stride 1: Conv1 -> Refresh(Activation) -> Conv2 + ShortcutStC -> Add -> Refresh(Activation)
 * - Stride 2: SplitAndQuadruple -> Downsample 1x1 -> Refresh(Identity) [Shortcut]
 *                               -> Conv1 -> Refresh(Activation) -> Conv2 + ShortcutStC -> Add -> Refresh(Activation)
 *
 * Both variants expose an identical, uniform `Forward` interface to callers.
 */
class ResidualBlock {
 public:
  ResidualBlock(ConstContextPtr<uint64_t> context,
                const Func15RBootExecution &refresh,
                const BootParameter &profile,
                std::shared_ptr<const ConvStCStage> stc_stage,
                const MaskPair *masks,
                std::shared_ptr<const SplitAndQuadruple> split,
                const TensorShape &in_shape,
                const TensorShape &out_shape,
                int stride,
                const ConvLayer &conv1_layer,
                const ConvLayer &conv2_layer,
                const std::string &name = "block");

  ~ResidualBlock();

  struct Timing {
    double total_ms = 0.0;
    double linear_ms = 0.0;
    double bootstrap_ms = 0.0;
  };

  const std::string &GetName() const { return name_; }
  int GetStride() const { return stride_; }
  const TensorShape &GetInShape() const { return in_shape_; }
  const TensorShape &GetOutShape() const { return out_shape_; }

  void AddRequiredRotations(EvkRequest &req) const;

  void Forward(Ciphertext<uint64_t> &out,
               const Ciphertext<uint64_t> &in,
               const EvkMap<uint64_t> &keys,
               Timing *timing = nullptr) const;

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  const Func15RBootExecution &refresh_;
  BootParameter profile_;
  std::shared_ptr<const ConvStCStage> stc_stage_;
  const MaskPair *masks_ = nullptr;
  std::shared_ptr<const SplitAndQuadruple> split_;
  TensorShape in_shape_;
  TensorShape out_shape_;
  int stride_;
  std::string name_;
  Constant<uint64_t> activation_gain_;
  std::unique_ptr<LinearTransform<uint64_t>> downsample_kernel_;
  Plaintext<uint64_t> downsample_bias_;

  // Block convolutions
  std::unique_ptr<LinearTransform<uint64_t>> conv1_kernel_;
  Plaintext<uint64_t> conv1_bias_;
  std::unique_ptr<LinearTransform<uint64_t>> conv2_kernel_;
  Plaintext<uint64_t> conv2_bias_;

  ConstContextPtr<uint64_t> GetContext() const;
  void Refresh(Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &in,
               FunctionalMode mode, const EvkMap<uint64_t> &keys) const;
};

}  // namespace cnn
}  // namespace cheddar
