#include "extension/MEBootEvalMod.h"

#include <cmath>
#include <limits>

#include "common/Assert.h"
#include "common/CommonUtils.h"

namespace cheddar {

MEBootEvalMod::MEBootEvalMod(ConstContextPtr<uint64_t> context,
                             const BootParameter &profile)
    : context_(context),
      profile_(profile),
      polynomial_scale_(profile.Mod1PolynomialTargetScale()),
      polynomial_(profile.coefficients, profile.mod1_level,
                  std::ldexp(BootParameter::kMod1Scale, profile.basis_lift),
                  polynomial_scale_, true,
                  std::numeric_limits<double>::denorm_min()) {
  // Preserve every nonzero exported coefficient, including the degree-62 tail.
  context->encoder_.EncodeConstant(offset_, profile.mod1_level,
                                  BootParameter::kMod1Scale,
                                  -0.25 / profile.k);
  if (profile.basis_lift) {
    context->encoder_.EncodeConstant(lift_, profile.mod1_level,
                                    std::ldexp(1.0, profile.basis_lift), 1.0);
  }
  polynomial_.Compile(context);
  int level = profile.mod1_level - Log2Ceil(profile.coefficients.size());
  double scale = polynomial_scale_;
  double root = profile.sqrt2pi;
  for (int i = 0; i < profile.double_angle; ++i) {
    root *= root;
    double_angle_.emplace_back(context, 2, -root, level, scale, level, scale);
    scale = double_angle_.back().final_scale_;
    --level;
  }
  context->AssertSameScale(scale, BootParameter::kMod1Scale);
  AssertTrue(level == profile.OutputLevel(), "Selected Mod1 depth mismatch");
}

void MEBootEvalMod::Evaluate(Ciphertext<uint64_t> &res,
                             const Ciphertext<uint64_t> &input,
                             const EvaluationKey<uint64_t> &key,
                             const std::function<void(int, const Ciphertext<uint64_t> &)> &observe) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Selected Mod1 context expired");
  AssertTrue(input.GetNP() == context->param_.LevelToNP(profile_.mod1_level),
             "Selected Mod1 input basis mismatch");
  context->AssertSameScale(input, BootParameter::kModUpScale);
  Ciphertext<uint64_t> normalized;
  context->Copy(normalized, input);
  // This binding is a division by the Mod1 message ratio, not a scale lift.
  normalized.SetScale(BootParameter::kMod1Scale);
  context->Add(normalized, normalized, offset_);
  if (profile_.basis_lift) {
    // Encode one at scale two: multiply residues and scale by two together.
    context->MultUnsafe(normalized, normalized, lift_, profile_.mod1_level);
  }
  if (observe) observe(0, normalized);
  polynomial_.Evaluate(context, res, normalized, key);
  if (observe) observe(1, res);
  int stage = 2;
  for (const auto &step : double_angle_) {
    step.Evaluate(context, res, res, res, key);
    if (observe) observe(stage, res);
    ++stage;
  }
  context->AssertSameScale(res, BootParameter::kMod1Scale);
  // Reference multiplies values back by the message ratio at this boundary.
  res.SetScale(BootParameter::kModUpScale);
}

double MEBootEvalMod::PlainEvaluate(double normalized_input) const {
  double value = polynomial_.PlainEvaluate(normalized_input - 0.25 / profile_.k);
  double root = profile_.sqrt2pi;
  for (int i = 0; i < profile_.double_angle; ++i) {
    root *= root;
    value = 2 * value * value - root;
  }
  return value;
}

}  // namespace cheddar
