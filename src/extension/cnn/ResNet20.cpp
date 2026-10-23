#include "extension/cnn/ResNet20.h"

#include <chrono>

#include "common/Assert.h"

namespace cheddar {
namespace cnn {

ResNet20Classifier::ResNet20Classifier(ConstContextPtr<uint64_t> context,
                                       const Func15RBootExecution &refresh,
                                       const BootParameter &profile,
                                       const ResNet20Weights &weights)
    : context_(context), refresh_(refresh), profile_(profile) {
  context->encoder_.EncodeConstant(activation_gain_, refresh.OutputLevel(),
                                   1.0, kActivationGain);

  const int widths[3] = {32, 16, 8};
  const int channels[3] = {16, 32, 64};
  TensorShape shapes[3];
  for (int g = 0; g < 3; ++g) {
    shapes[g] = TensorShape::Create(widths[g], channels[g], profile.slots);
    stages_[g] = std::make_shared<ConvStCStage>(context, shapes[g], profile);
    masks_[g] = stages_[g]->CreateMaskPlaintexts();
  }

  for (int s = 0; s < 2; ++s) {
    splits_[s] = std::make_shared<SplitAndQuadruple>(context, shapes[s], profile);
  }

  // 1. Stem (32x32, 16 channels, shares Stage 0)
  stem_kernel_ = std::make_unique<LinearTransform<uint64_t>>(
      stages_[0]->CreateFusedKernelTransform(weights.stem));
  stem_bias_ = stages_[0]->CreateBiasPlaintext(weights.stem.bias);

  // 2. 9 Deep Residual Blocks
  for (int i = 0; i < kBlocks; ++i) {
    const int g = i / 3;
    const int b = i % 3;
    const bool is_transition = (b == 0 && g > 0);
    const auto &in_shape = is_transition ? shapes[g - 1] : shapes[g];
    const auto &out_shape = shapes[g];
    const int stride = is_transition ? 2 : 1;
    auto split = is_transition ? splits_[g - 1] : nullptr;
    const std::string name = "g" + std::to_string(g + 1) + "-b" + std::to_string(b);

    blocks_[i] = std::make_unique<ResidualBlock>(
        context, refresh, profile, stages_[g], &masks_[g], split,
        in_shape, out_shape, stride, weights.conv1[i], weights.conv2[i], name);
  }

  // 3. Classification Head
  pooling_ = std::make_unique<AveragePooling>(context, profile);
  fc_ = std::make_unique<FullyConnected>(context, profile, weights.fc_weight,
                                         weights.fc_bias);
}

ResNet20Classifier::~ResNet20Classifier() = default;

std::vector<std::string> ResNet20Classifier::RefreshSites() {
  return {
      "stem",
      "g1-b0-act", "g1-b0-out",
      "g1-b1-act", "g1-b1-out",
      "g1-b2-act", "g1-b2-out",
      "g2-b0-ds",  "g2-b0-act", "g2-b0-out",
      "g2-b1-act", "g2-b1-out",
      "g2-b2-act", "g2-b2-out",
      "g3-b0-ds",  "g3-b0-act", "g3-b0-out",
      "g3-b1-act", "g3-b1-out",
      "g3-b2-act", "g3-b2-out",
  };
}

ConstContextPtr<uint64_t> ResNet20Classifier::GetContext() const {
  auto ctx = context_.lock();
  AssertTrue(ctx != nullptr, "ResNet20Classifier: Context has been destroyed");
  return ctx;
}

void ResNet20Classifier::AddRequiredRotations(EvkRequest &req) const {
  for (const auto &stage : stages_) {
    stage->AddRequiredRotations(req);
  }
  for (const auto &split : splits_) {
    split->AddRequiredRotations(req);
  }
  stem_kernel_->AddRequiredRotations(req);
  for (const auto &block : blocks_) {
    block->AddRequiredRotations(req);
  }
  pooling_->AddRequiredRotations(req);
  fc_->AddRequiredRotations(req);
}

void ResNet20Classifier::Refresh(Ciphertext<uint64_t> &out,
                                 const Ciphertext<uint64_t> &in,
                                 FunctionalMode mode,
                                 const EvkMap<uint64_t> &keys) const {
  auto context = GetContext();
  Ciphertext<uint64_t> refreshed;
  refresh_.EvaluatePostStC(refreshed, in, keys, mode, /*real_only=*/false);
  context->MultUnsafe(out, refreshed, activation_gain_, refresh_.OutputLevel());
  out.SetScale(refreshed.GetScale());
  out.SetNumSlots(refreshed.GetNumSlots());
}

void ResNet20Classifier::Classify(Ciphertext<uint64_t> &logits,
                                  const Ciphertext<uint64_t> &input,
                                  const EvkMap<uint64_t> &keys,
                                  InferenceTiming *timing) const {
  auto context = GetContext();
  AssertTrue(input.GetNP() == context->param_.LevelToNP(2),
             "ResNet20 input must be Level 2");

  if (timing) {
    *timing = InferenceTiming{};
  }

  auto now = []() { return std::chrono::high_resolution_clock::now(); };
  auto elapsed_ms = [](auto start) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::high_resolution_clock::now() - start)
        .count();
  };

  auto t_start = now();

  // 1. Stem
  auto t_stem = now();
  Ciphertext<uint64_t> h, stc_stem;
  auto t_stem_lin = now();
  stages_[0]->ConvStC(stc_stem, input, *stem_kernel_, &stem_bias_, nullptr, keys);
  double stem_lin_ms = elapsed_ms(t_stem_lin);
  auto t_stem_boot = now();
  Refresh(h, stc_stem, FunctionalMode::kActivation, keys);
  double stem_boot_ms = elapsed_ms(t_stem_boot);
  if (timing) timing->stem_ms = elapsed_ms(t_stem);

  // 2. 9 Deep Residual Blocks
  auto t_blocks = now();
  double blocks_lin_ms = 0.0;
  double blocks_boot_ms = 0.0;
  for (int i = 0; i < kBlocks; ++i) {
    auto t_b = now();
    ResidualBlock::Timing b_timing;
    blocks_[i]->Forward(h, h, keys, timing ? &b_timing : nullptr);
    if (timing) {
      timing->block_ms[i] = elapsed_ms(t_b);
      blocks_lin_ms += b_timing.linear_ms;
      blocks_boot_ms += b_timing.bootstrap_ms;
    }
  }
  if (timing) timing->blocks_ms = elapsed_ms(t_blocks);

  // 3. Classification Head (AvgPool + FC)
  auto t_head = now();
  Ciphertext<uint64_t> pooled;
  pooling_->Pool(pooled, h, keys);
  fc_->Classify(logits, pooled, keys);
  double head_lin_ms = elapsed_ms(t_head);

  if (timing) {
    timing->stage1_ms = timing->block_ms[0] + timing->block_ms[1] + timing->block_ms[2];
    timing->stage2_ms = timing->block_ms[3] + timing->block_ms[4] + timing->block_ms[5];
    timing->stage3_ms = timing->block_ms[6] + timing->block_ms[7] + timing->block_ms[8];
    timing->pool_fc_ms = head_lin_ms;
    timing->total_ms = elapsed_ms(t_start);
    timing->total_bootstrap_ms = stem_boot_ms + blocks_boot_ms;
    timing->total_linear_ms = stem_lin_ms + blocks_lin_ms + head_lin_ms;
    timing->activation_refreshes = 19;
    timing->identity_refreshes = 2;
  }
}

}  // namespace cnn
}  // namespace cheddar
