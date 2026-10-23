#include "extension/cnn/ResidualBlock.h"

#include <chrono>

#include "common/Assert.h"

namespace cheddar {
namespace cnn {

namespace {

ConvLayer MakeOptionADownsample(int in_ch, int out_ch) {
  ConvLayer layer;
  layer.in_ch = in_ch;
  layer.out_ch = out_ch;
  layer.stride = 2;
  layer.bias.assign(out_ch, 0.0);
  layer.weight.assign(
      out_ch, std::vector<std::vector<std::vector<double>>>(
                  in_ch, std::vector<std::vector<double>>(
                             3, std::vector<double>(3, 0.0))));
  const int offset = (out_ch - in_ch) / 2;
  for (int i = 0; i < in_ch; ++i) {
    layer.weight[i + offset][i][1][1] = 1.0;
  }
  return layer;
}

}  // namespace

ResidualBlock::ResidualBlock(ConstContextPtr<uint64_t> context,
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
                             const std::string &name)
    : context_(context),
      refresh_(refresh),
      profile_(profile),
      stc_stage_(stc_stage),
      masks_(masks),
      split_(split),
      in_shape_(in_shape),
      out_shape_(out_shape),
      stride_(stride),
      name_(name) {
  AssertTrue(stride == 1 || stride == 2, "ResidualBlock stride must be 1 or 2");
  AssertTrue(stc_stage_ != nullptr, "ResidualBlock: stc_stage must not be null");
  AssertTrue(masks_ != nullptr, "ResidualBlock: masks must not be null");
  if (stride_ == 2) {
    AssertTrue(split_ != nullptr, "ResidualBlock: stride-2 requires split operator");
  }

  context->encoder_.EncodeConstant(activation_gain_, refresh.OutputLevel(),
                                   1.0, 120.0);

  if (stride_ == 2) {
    auto down_layer = MakeOptionADownsample(in_shape.channels, out_shape.channels);
    downsample_kernel_ = std::make_unique<LinearTransform<uint64_t>>(
        stc_stage_->CreateSplitKernelTransform(down_layer, in_shape));
    downsample_bias_ = stc_stage_->CreateBiasPlaintext(down_layer.bias);

    conv1_kernel_ = std::make_unique<LinearTransform<uint64_t>>(
        stc_stage_->CreateSplitKernelTransform(conv1_layer, in_shape));
  } else {
    conv1_kernel_ = std::make_unique<LinearTransform<uint64_t>>(
        stc_stage_->CreateFusedKernelTransform(conv1_layer));
  }
  conv1_bias_ = stc_stage_->CreateBiasPlaintext(conv1_layer.bias);

  conv2_kernel_ = std::make_unique<LinearTransform<uint64_t>>(
      stc_stage_->CreateFusedKernelTransform(conv2_layer));
  conv2_bias_ = stc_stage_->CreateBiasPlaintext(conv2_layer.bias);
}

ResidualBlock::~ResidualBlock() = default;

ConstContextPtr<uint64_t> ResidualBlock::GetContext() const {
  auto ctx = context_.lock();
  AssertTrue(ctx != nullptr, "ResidualBlock: Context has been destroyed");
  return ctx;
}

void ResidualBlock::AddRequiredRotations(EvkRequest &req) const {
  stc_stage_->AddRequiredRotations(req);
  if (split_) {
    split_->AddRequiredRotations(req);
    downsample_kernel_->AddRequiredRotations(req);
  }
  conv1_kernel_->AddRequiredRotations(req);
  conv2_kernel_->AddRequiredRotations(req);
}

void ResidualBlock::Refresh(Ciphertext<uint64_t> &out,
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

void ResidualBlock::Forward(Ciphertext<uint64_t> &out,
                            const Ciphertext<uint64_t> &in,
                            const EvkMap<uint64_t> &keys,
                            Timing *timing) const {
  auto context = GetContext();

  auto now = []() { return std::chrono::high_resolution_clock::now(); };
  auto elapsed_ms = [](auto start) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::high_resolution_clock::now() - start)
        .count();
  };

  auto t_start = now();
  double t_lin = 0.0;
  double t_boot = 0.0;

  if (stride_ == 2) {
    // 1. Split & Quadruple quadrant folding (Level 3 -> Level 2)
    auto t0 = now();
    Ciphertext<uint64_t> split;
    split_->Evaluate(split, in, keys);

    // 2. Option-A Downsample shortcut -> Identity Refresh
    Ciphertext<uint64_t> stc_ds, shortcut_src;
    stc_stage_->ConvStC(stc_ds, split, *downsample_kernel_, &downsample_bias_,
                        nullptr, keys);
    if (timing) t_lin += elapsed_ms(t0);

    auto t1 = now();
    Refresh(shortcut_src, stc_ds, FunctionalMode::kIdentity, keys);
    if (timing) t_boot += elapsed_ms(t1);

    // 3. Conv1 on split -> Activation Refresh
    auto t2 = now();
    Ciphertext<uint64_t> stc_conv1, conv1_act;
    stc_stage_->ConvStC(stc_conv1, split, *conv1_kernel_, &conv1_bias_,
                        nullptr, keys);
    if (timing) t_lin += elapsed_ms(t2);

    auto t3 = now();
    Refresh(conv1_act, stc_conv1, FunctionalMode::kActivation, keys);
    if (timing) t_boot += elapsed_ms(t3);

    // 4. Conv2 on conv1_act -> Residual addition with ShortcutStC
    auto t4 = now();
    Ciphertext<uint64_t> residual, shortcut, sum;
    stc_stage_->ConvStC(residual, conv1_act, *conv2_kernel_, &conv2_bias_,
                        masks_, keys);
    stc_stage_->ShortcutStC(shortcut, shortcut_src, keys);
    context->Add(sum, residual, shortcut);
    if (timing) t_lin += elapsed_ms(t4);

    // 5. Block output Activation Refresh
    auto t5 = now();
    Refresh(out, sum, FunctionalMode::kActivation, keys);
    if (timing) t_boot += elapsed_ms(t5);
  } else {
    // 1. Conv1 on input -> Activation Refresh
    auto t0 = now();
    Ciphertext<uint64_t> stc_conv1, conv1_act;
    stc_stage_->ConvStC(stc_conv1, in, *conv1_kernel_, &conv1_bias_,
                        masks_, keys);
    if (timing) t_lin += elapsed_ms(t0);

    auto t1 = now();
    Refresh(conv1_act, stc_conv1, FunctionalMode::kActivation, keys);
    if (timing) t_boot += elapsed_ms(t1);

    // 2. Conv2 on conv1_act -> Residual addition with ShortcutStC
    auto t2 = now();
    Ciphertext<uint64_t> residual, shortcut, sum;
    stc_stage_->ConvStC(residual, conv1_act, *conv2_kernel_, &conv2_bias_,
                        masks_, keys);
    stc_stage_->ShortcutStC(shortcut, in, keys);
    context->Add(sum, residual, shortcut);
    if (timing) t_lin += elapsed_ms(t2);

    // 3. Block output Activation Refresh
    auto t3 = now();
    Refresh(out, sum, FunctionalMode::kActivation, keys);
    if (timing) t_boot += elapsed_ms(t3);
  }

  if (timing) {
    timing->linear_ms = t_lin;
    timing->bootstrap_ms = t_boot;
    timing->total_ms = elapsed_ms(t_start);
  }
}

}  // namespace cnn
}  // namespace cheddar
