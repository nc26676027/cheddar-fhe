#include "extension/EvalMod.h"

#include "common/Assert.h"
#include "common/CommonUtils.h"

namespace cheddar {

template <typename word>
double EvalMod<word>::ComputeStartScale(const Parameter<word> &param,
                                       const BootParameter &boot_param) {
  int level = boot_param.GetEvalModStartLevel();
  double target_bits = std::round(std::log2(param.GetRescalePrimeProd(level)));
  if (!boot_param.use_meboot_cts_ && boot_param.num_double_angle_ == 3)
    return std::exp2(target_bits);
  int depth = boot_param.GetNumEvalModLevels();
  double start_bits = target_bits / std::exp2(depth);
  for (int i = 0; i < depth; ++i) {
    start_bits += std::log2(param.GetRescalePrimeProd(level - i)) / std::exp2(i + 1);
  }
  return std::exp2(start_bits);
}

template <typename word>
EvalMod<word>::EvalMod(ConstContextPtr<word> context,
                       const BootParameter &boot_param) {
  // Do not check here, checking handled by the BootContext
  int start_level = boot_param.GetEvalModStartLevel();
  int num_double_angle = boot_param.num_double_angle_;
  int double_angle_ratio = 1 << num_double_angle;
  start_scale_ = ComputeStartScale(context->param_, boot_param);
  AssertTrue(std::isfinite(start_scale_) && start_scale_ > 0,
             "Invalid EvalMod start scale");

  // See the development notes for details
  int actual_K = (1 << boot_param.num_double_angle_) * boot_param.initial_K_;
  context->encoder_.EncodeConstant(initial_const_, start_level, start_scale_,
                                   -0.25 / actual_K);

  const auto &mod_coefficients = boot_param.mod_coefficients_;
  int mod_levels = Log2Ceil(mod_coefficients.size());

  double target_scale = start_scale_;
  for (int i = 0; i < mod_levels; i++) {
    target_scale = target_scale * target_scale /
                   context->param_.GetRescalePrimeProd(start_level - i);
  }

  mod_functions_.emplace_back(boot_param.mod_coefficients_, start_level,
                              start_scale_, target_scale, true,
                              boot_param.num_double_angle_ == 5 ? 1e-15
                                                               : kZeroCoeffThreshold);
  mod_functions_[0].Compile(context);

  // TODO(jongmin.kim): add support for other evalmod functions.

  double sqrt2pi = std::pow(0.5 / M_PI, 1.0 / double_angle_ratio);
  for (int i = 0; i < num_double_angle; i++) {
    // 2 x^2 - 1
    sqrt2pi *= sqrt2pi;
    int double_angle_level = start_level - mod_levels - i;
    double_angle_.emplace_back(context, 2, -sqrt2pi, double_angle_level,
                               target_scale, double_angle_level, target_scale);
    target_scale = target_scale * target_scale /
                   context->param_.GetRescalePrimeProd(double_angle_level);
  }
  end_scale_ = target_scale;
  AssertTrue(std::isfinite(end_scale_) && end_scale_ > 0,
             "Invalid EvalMod end scale");
}

template <typename word>
void EvalMod<word>::Evaluate(ConstContextPtr<word> context, Ct &res,
                             const Ct &input, const Evk &mult_key) {
  context->Add(res, input, initial_const_);
  mod_functions_[0].Evaluate(context, res, res, mult_key);
  for (const auto &da : double_angle_) {
    da.Evaluate(context, res, res, res, mult_key);
  }
}

template <typename word>
int EvalMod<word>::GetEvalModPolyDegree(int poly_index /*= 0*/) const {
  return mod_functions_.at(poly_index).GetPolyDegree();
}

template <typename word>
int EvalMod<word>::GetNumDoubleAngle() const {
  return double_angle_.size();
}

template class EvalMod<uint32_t>;
template class EvalMod<uint64_t>;

}  // namespace cheddar
