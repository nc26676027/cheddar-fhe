#pragma once

#include <array>
#include <memory>
#include <vector>

#include "core/Context.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"
#include "extension/BootParameter.h"
#include "extension/Func15RBootExecution.h"
#include "extension/cnn/AveragePooling.h"
#include "extension/cnn/ConvStCFusion.h"
#include "extension/cnn/FullyConnected.h"
#include "extension/cnn/ResidualBlock.h"
#include "extension/cnn/TensorShape.h"

namespace cheddar {
namespace cnn {

/**
 * @brief Folded (BN-merged) ResNet-20 checkpoint.
 */
struct ResNet20Weights {
  static constexpr int kBlocks = 9;
  ConvLayer stem;
  std::array<ConvLayer, kBlocks> conv1;
  std::array<ConvLayer, kBlocks> conv2;
  std::vector<double> fc_weight;  // [10][64] row-major
  std::vector<double> fc_bias;    // [10]
};

/**
 * @brief Encrypted ResNet-20 CIFAR-10 classifier.
 * Deep composite module orchestrating Stem -> 9 ResidualBlocks -> AvgPool -> FC.
 */
class ResNet20Classifier {
 public:
  static constexpr int kBlocks = 9;
  static constexpr double kActivationGain = 120.0;

  struct InferenceTiming {
    double total_ms = 0.0;
    double stem_ms = 0.0;
    double stage1_ms = 0.0;
    double stage2_ms = 0.0;
    double stage3_ms = 0.0;
    double blocks_ms = 0.0;
    double pool_fc_ms = 0.0;
    double total_bootstrap_ms = 0.0;
    double total_linear_ms = 0.0;
    int activation_refreshes = 0;
    int identity_refreshes = 0;
    std::array<double, kBlocks> block_ms{};
  };

  ResNet20Classifier(ConstContextPtr<uint64_t> context,
                     const Func15RBootExecution &refresh,
                     const BootParameter &profile,
                     const ResNet20Weights &weights);
  ~ResNet20Classifier();

  static TensorShape InputShape() { return TensorShape::Create(32, 16); }
  static std::vector<std::string> RefreshSites();

  const ResidualBlock &GetBlock(int index) const { return *blocks_[index]; }

  void AddRequiredRotations(EvkRequest &req) const;

  void Classify(Ciphertext<uint64_t> &logits,
                const Ciphertext<uint64_t> &input,
                const EvkMap<uint64_t> &keys,
                InferenceTiming *timing = nullptr) const;

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  const Func15RBootExecution &refresh_;
  BootParameter profile_;
  Constant<uint64_t> activation_gain_;

  // 3 StC stages (one per spatial dimension: 32x32, 16x16, 8x8)
  std::array<std::shared_ptr<ConvStCStage>, 3> stages_;
  std::array<MaskPair, 3> masks_;

  // 2 SplitAndQuadruple operators for stride-2 transitions
  std::array<std::shared_ptr<SplitAndQuadruple>, 2> splits_;

  // Stem
  std::unique_ptr<LinearTransform<uint64_t>> stem_kernel_;
  Plaintext<uint64_t> stem_bias_;

  // 9 Deep Residual Blocks
  std::array<std::unique_ptr<ResidualBlock>, kBlocks> blocks_;

  // Head
  std::unique_ptr<AveragePooling> pooling_;
  std::unique_ptr<FullyConnected> fc_;

  ConstContextPtr<uint64_t> GetContext() const;
  void Refresh(Ciphertext<uint64_t> &out, const Ciphertext<uint64_t> &in,
               FunctionalMode mode, const EvkMap<uint64_t> &keys) const;
};

}  // namespace cnn
}  // namespace cheddar
