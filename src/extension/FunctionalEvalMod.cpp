#include "extension/FunctionalEvalMod.h"

#include <cmath>
#include <limits>

#include "common/Assert.h"
#include "common/CommonUtils.h"

namespace cheddar {

FunctionalEvalMod::FunctionalEvalMod(ConstContextPtr<uint64_t> context,
                                     const BootParameter &profile)
    : context_(context),
      profile_(profile),
      main_chebyshev_([&]() {
        std::vector<double> coeffs(
            func15_rboot::kMainChebyshevCoeffs,
            func15_rboot::kMainChebyshevCoeffs + func15_rboot::kMainDegree + 1);
        double target = profile.application_scale;
        for (int i = 0; i < profile.double_angle; ++i) {
          target = std::sqrt(target * profile.q[8 + 1 + i]);
        }
        return EvalPoly<uint64_t>(
            coeffs, profile.mod1_level,
            std::ldexp(profile.application_scale, profile.basis_lift), target,
            /*chebyshev=*/true, std::numeric_limits<double>::denorm_min());
      }()),
      odd_tail_([&]() {
        std::vector<double> coeffs(
            func15_rboot::kOddTailCoeffs,
            func15_rboot::kOddTailCoeffs + func15_rboot::kOddTailDegree + 1);
        const double tail_gain =
            func15_rboot::kGain * func15_rboot::kFinalRatio;
        for (auto &c : coeffs) c *= tail_gain;
        // Tail input scale at level 7 is approximately 2^44
        double target = profile.application_scale;
        for (int i = 0; i < profile.double_angle; ++i) {
          target = std::sqrt(target * profile.q[8 + 1 + i]);
        }
        double s9 = (target * target) / profile.q[10];
        double s8 = (s9 * s9) / profile.q[9];
        double s7 = (s8 * s8) / profile.q[8];
        double tail_input_scale = s7 * 16.0;
        return EvalPoly<uint64_t>(
            coeffs, 7, tail_input_scale, profile.application_scale,
            /*chebyshev=*/false, std::numeric_limits<double>::denorm_min());
      }()),
      even_tail_([&]() {
        std::vector<double> coeffs(
            func15_rboot::kEvenArcChebyshevCoeffs,
            func15_rboot::kEvenArcChebyshevCoeffs +
                func15_rboot::kEvenArcDegree + 1);
        const double tail_gain =
            func15_rboot::kGain * func15_rboot::kFinalRatio;
        for (size_t i = 0; i < coeffs.size(); ++i) {
          if (i % 2 == 0) {
            coeffs[i] = 0.0;  // Strictly odd Chebyshev tail as in Lattigo
          } else {
            coeffs[i] *= tail_gain;
          }
        }
        double target = profile.application_scale;
        for (int i = 0; i < profile.double_angle; ++i) {
          target = std::sqrt(target * profile.q[8 + 1 + i]);
        }
        double s9 = (target * target) / profile.q[10];
        double s8 = (s9 * s9) / profile.q[9];
        double s7 = (s8 * s8) / profile.q[8];
        double tail_input_scale = s7 * 16.0;
        return EvalPoly<uint64_t>(
            coeffs, 7, tail_input_scale, profile.application_scale,
            /*chebyshev=*/true, std::numeric_limits<double>::denorm_min());
      }()) {
  AssertTrue(profile.route == BootRoute::kFunc15RBoot,
             "FunctionalEvalMod requires Func15RBoot route");
  AssertTrue(profile.mod1_level == 16 && profile.double_angle == 2,
             "FunctionalEvalMod requires Level 16 and DA=2");

  double target = profile.application_scale;
  for (int i = 0; i < profile.double_angle; ++i) {
    target = std::sqrt(target * profile.q[8 + 1 + i]);
  }
  polynomial_scale_ = target;

  // 1. Encode entry constants
  context->encoder_.EncodeConstant(offset_const_, profile.mod1_level,
                                  profile.application_scale,
                                  func15_rboot::kOffset);
  context->encoder_.EncodeConstant(
      cosine_lift_const_, profile.mod1_level,
      std::ldexp(1.0, profile.basis_lift), 1.0);

  // 2. Compile main degree-62 Chebyshev polynomial
  main_chebyshev_.Compile(context);

  // 3. Double-angle reductions (Level 10 -> 9, Level 9 -> 8)
  int level = profile.mod1_level - Log2Ceil(func15_rboot::kMainDegree + 1);
  AssertTrue(level == 10, "Main Chebyshev output level mismatch");
  double scale = polynomial_scale_;
  double root = func15_rboot::kSqrt2Pi;
  for (int i = 0; i < profile.double_angle; ++i) {
    root *= root;
    double_angle_.emplace_back(context, 2, -root, level, scale, level, scale);
    scale = double_angle_.back().final_scale_;
    --level;
  }
  AssertTrue(level == 8, "Double angle output level mismatch");

  // 4. Level 8 branch: even DA step and odd scale-up & rescale
  root *= root;
  da_even_step_ =
      std::make_unique<AXYPBZ<uint64_t>>(context, 2, -root, 8, scale, 8, scale);

  context->encoder_.EncodeConstant(phase_lift_const_, 8,
                                  profile.application_scale, 1.0);
  context->encoder_.EncodeConstant(tail_lift_const_, 7, 16.0, 1.0);

  // 5. Compile degree-15 tails
  odd_tail_.Compile(context);
  even_tail_.Compile(context);

  // 6. Output shift and 0.0625 constant at Level 3
  const double const_val_0_0625 = 0.0625 * func15_rboot::kFinalRatio;
  context->encoder_.EncodeConstant(const_0_0625_, profile.OutputLevel(),
                                  profile.application_scale,
                                  const_val_0_0625);
  context->encoder_.EncodeConstant(shift_const_, profile.OutputLevel(),
                                  profile.application_scale,
                                  func15_rboot::kOutputShift);
}

void FunctionalEvalMod::Evaluate(
    Ciphertext<uint64_t> &res,
    const Ciphertext<uint64_t> &input,
    const EvaluationKey<uint64_t> &mult_key,
    FunctionalMode mode,
    const std::function<void(const char *, const Ciphertext<uint64_t> &)> &observe) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "FunctionalEvalMod context expired");
  int in_level = context->param_.NPToLevel(input.GetNP());
  AssertTrue(in_level >= profile_.mod1_level,
             "FunctionalEvalMod input level below mod1_level");

  Ciphertext<uint64_t> x;
  if (in_level > profile_.mod1_level) {
    context->DropLevel(x, input, profile_.mod1_level);
  } else {
    context->Copy(x, input);
  }
  x.SetScale(profile_.application_scale);

  if (observe) observe("func15-input", x);

  // 1. Add center offset (-0.25 / K)
  context->Add(x, x, offset_const_);
  if (observe) observe("func15-offset", x);

  // 2. Cosine scale lift (3 bits)
  context->MultUnsafe(x, x, cosine_lift_const_, profile_.mod1_level);
  if (observe) observe("func15-lifted", x);

  // 3. Chebyshev degree-62 evaluation: Level 16 -> Level 10
  Ciphertext<uint64_t> z;
  main_chebyshev_.Evaluate(context, z, x, mult_key);
  if (observe) observe("func15-chebyshev-out", z);

  // 4. Double angle steps: Level 10 -> 9, Level 9 -> 8
  for (size_t i = 0; i < double_angle_.size(); ++i) {
    double_angle_[i].Evaluate(context, z, z, z, mult_key);
    if (observe) observe(("func15-da-" + std::to_string(i + 1)).c_str(), z);
  }

  // 5. Level 8 branch: even DA step -> Level 7
  Ciphertext<uint64_t> even;
  if (mode == FunctionalMode::kActivation) {
    da_even_step_->Evaluate(context, even, z, z, mult_key);
    if (observe) observe("func15-da-even", even);
  }

  // Odd branch at Level 8: scale up by phase (2^43) and rescale -> Level 7
  context->MultUnsafe(z, z, phase_lift_const_, 8);
  context->Rescale(z, z);
  if (observe) observe("func15-odd-level7", z);

  // 6. Tail boost (4 bits at Level 7)
  context->MultUnsafe(z, z, tail_lift_const_, 7);
  if (mode == FunctionalMode::kActivation) {
    context->MultUnsafe(even, even, tail_lift_const_, 7);
  }
  if (observe) observe("func15-tail-boosted", z);

  // 7. Tail evaluations: Level 7 -> Level 3
  Ciphertext<uint64_t> odd_res;
  odd_tail_.Evaluate(context, odd_res, z, mult_key);
  if (observe) observe("func15-odd-tail-out", odd_res);

  if (mode == FunctionalMode::kIdentity) {
    // Identity: 4 * odd_res
    context->Add(res, odd_res, odd_res);
    context->Add(res, res, res);
    res.SetScale(profile_.application_scale);
    if (observe) observe("func15-identity-out", res);
    return;
  }

  // Activation: 2 * odd_res + even_res + 0.0625*ratio + output_shift
  Ciphertext<uint64_t> even_res;
  even_tail_.Evaluate(context, even_res, even, mult_key);
  if (observe) observe("func15-even-tail-out", even_res);

  context->Add(res, odd_res, odd_res);
  context->Add(res, res, even_res);
  context->Add(res, res, const_0_0625_);
  context->Add(res, res, shift_const_);
  res.SetScale(profile_.application_scale);
  if (observe) observe("func15-activation-out", res);
}

double FunctionalEvalMod::PlainEvaluate(double x, FunctionalMode mode) const {
  const double arg = x * func15_rboot::kInputGain + func15_rboot::kOffset;
  double z = main_chebyshev_.PlainEvaluate(arg);
  double c = func15_rboot::kSqrt2Pi;
  for (int i = 0; i < func15_rboot::kDoubleAngle; ++i) {
    c *= c;
    z = 2.0 * z * z - c;
  }
  const double odd = odd_tail_.PlainEvaluate(z);
  if (mode == FunctionalMode::kIdentity) {
    return 4.0 * odd;
  }
  c *= c;
  const double cosine = 2.0 * z * z - c;
  const double arc = even_tail_.PlainEvaluate(cosine);
  return 2.0 * odd + arc + 0.0625 * func15_rboot::kFinalRatio +
         func15_rboot::kOutputShift;
}

}  // namespace cheddar
