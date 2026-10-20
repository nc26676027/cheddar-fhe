#include "extension/DiscreteIdentityBootstrap.h"

#include <cmath>
#include "BootstrapTiming.h"
#include "common/Assert.h"
#include "extension/MEBootMatrixPlanner.h"

namespace cheddar {
namespace {
StripedMatrix Merge(const std::vector<StripedMatrix> &stages,
                    int begin, int width, double gain) {
  auto matrix = stages.at(begin);
  for (int index = 1; index < width; ++index)
    matrix = StripedMatrix::Mult(stages.at(begin + index), matrix);
  return StripedMatrix::Mult(matrix, gain);
}

void Apply(ConstContextPtr<uint64_t> context, Ciphertext<uint64_t> &result,
           const Ciphertext<uint64_t> &input, const EvkMap<uint64_t> &keys,
           const std::vector<LinearTransform<uint64_t>> &factors) {
  factors.front().Evaluate(context, result, input, keys);
  for (size_t index = 1; index < factors.size(); ++index)
    factors[index].Evaluate(context, result, result, keys);
}

DiscreteIdentityTiming ProfileEvaluate(
    const std::function<void(const DiscreteIdentityExecution::Observer &)> &evaluate) {
  return detail::BootstrapTiming::Measure<DiscreteIdentityTiming>(
      [&](const detail::BootstrapTiming::Marker &mark) {
        evaluate([&](const char *name, const DiscreteIdentityExecution::Ct &) {
          mark(name);
        });
      });
}
}  // namespace

DiscreteIdentityParameters DiscreteIdentityParameters::FromCoefficients(
    int width, std::vector<std::vector<double>> coefficients,
    const std::string &name) {
  DiscreteIdentityParameters result;
  result.width = width;
  result.input_scale = std::exp2(width == 6 ? 59 : 40);
  result.extraction_scale = std::exp2(width == 6 ? 32 : 40);
  result.max_input_error = std::exp2(-12);
  result.max_digit_error = std::exp2(-9);
  result.name = name;
  result.extractor_coefficients = std::move(coefficients);
  result.Validate();
  return result;
}

DiscreteIdentityParameters DiscreteIdentityParameters::FromHW40RemezCoefficients(
    std::vector<std::vector<double>> coefficients,
    int argument_divisor, int affine_scale, int affine_integer) {
  AssertTrue(argument_divisor == 43 && affine_scale == 1032 &&
                 affine_integer == 48,
             "HW40 Remez requires the certified (2*I+1)/43 argument map");
  DiscreteIdentityParameters result;
  result.width = 6;
  result.input_scale = std::ldexp(double(affine_scale), 49);
  result.extraction_scale = std::exp2(32);
  result.argument_gain = double(affine_integer) / affine_scale;
  result.extraction_baby_step = 16;
  result.max_input_error = std::exp2(-12);
  result.max_digit_error = std::exp2(-9);
  result.name = "meboot-discrete-hw40-remez-d63";
  result.extractor_coefficients = std::move(coefficients);
  result.Validate();
  return result;
}

void DiscreteIdentityParameters::Validate() const {
  AssertTrue(width >= 2 && width <= 6 && input_scale > 0 &&
                 std::isfinite(input_scale) &&
                 extraction_scale >= 1 && std::isfinite(extraction_scale) &&
                 max_input_error > 0 &&
                 max_input_error <= std::exp2(-12) && max_digit_error > 0 &&
                 max_digit_error <= std::exp2(-9) && !name.empty() &&
                 int(extractor_coefficients.size()) == width,
             "Invalid discrete identity profile");
  AssertTrue(argument_gain == 0 ||
                 (width == 6 && argument_gain == 2.0 / 43),
             "Unsupported discrete extractor argument map");
  if (width == 6) {
    AssertTrue(cleaner_alignment_level >= 21 && cleaner_alignment_level <= 29,
               "Unsupported discrete cleaner alignment level");
    AssertTrue(cleaning_rescales.size() == 2,
               "Discrete identity requires two cleaning iterations");
    for (int count : cleaning_rescales)
      AssertTrue(count >= 1 && count <= 2,
                 "Invalid discrete cleaning rescale schedule");
  }
  const size_t expected_degree = width == 5 ? 47 :
      width == 6 ? (argument_gain == 0 ? 95 : 63) : 0;
  for (const auto &polynomial : extractor_coefficients) {
    AssertTrue(!polynomial.empty() && polynomial.front() == 0.5 &&
                   (expected_degree == 0 ||
                    polynomial.size() == expected_degree + 1),
               "Discrete identity profile has an unexpected extractor degree");
    for (size_t degree = 2; degree < polynomial.size(); degree += 2)
      AssertTrue(polynomial[degree] == 0,
                 "Discrete identity profile requires odd extractor remainders");
    for (const auto coefficient : polynomial)
      AssertTrue(std::isfinite(coefficient),
                 "Discrete identity profile contains a non-finite coefficient");
  }
}

DiscreteIdentityExecution::DiscreteIdentityExecution(
    ConstContextPtr<uint64_t> context,
    std::unique_ptr<DiscreteIntegerCleaner> cleaner,
    Constant<uint64_t> bypass_alignment,
    Constant<uint64_t> normalize, double gap, int output_level,
    double output_scale)
    : context_(context), cleaner_(std::move(cleaner)),
      bypass_alignment_(std::move(bypass_alignment)),
      normalize_(std::move(normalize)), gap_(gap),
      output_level_(output_level), output_scale_(output_scale) {
  AssertTrue(cleaner_ != nullptr && output_level >= 0 && output_scale > 0 &&
                 std::isfinite(gap) && gap > 0,
             "Invalid discrete identity execution contract");
}

DiscreteIdentityExecution::DiscreteIdentityExecution(
    ConstContextPtr<uint64_t> context, const BootParameter &profile,
    const DiscreteIdentityParameters &parameters, BootTransforms &transforms)
    : context_(context) {
  parameters.Validate();
  AssertTrue(IsDiscreteMEBootRoute(profile.route) && parameters.width == 6,
             "Discrete Scaling Binding requires the six-bit MEBOOT route");
  profile.ValidateScalingBinding();
  const auto &parameter = context->param_;
  const double stc_scale = std::exp2(*profile.stc_output_log_scale);
  gap_ = double(profile.q[0]) /
         ((1.0 / 64) * stc_scale * profile.ScaleDownMultiplier());
  const double integer_units = double(profile.q[0]) * profile.ModUpMultiplier() /
                               BootParameter::kModUpScale;
  DiscreteCleaningParameters cleaning;
  cleaning.width = parameters.width;
  cleaner_alignment_level_ = parameters.cleaner_alignment_level;
  cleaner_input_scale_ = parameters.input_scale;
  const double argument_gain = parameters.argument_gain == 0 ?
      1 / std::exp2(parameters.width - 1) : parameters.argument_gain;
  const double alignment_scale =
      parameters.input_scale / (2 * BootParameter::kModUpScale);
  const double encoded_divisor = alignment_scale * argument_gain;
  cleaner_alignment_rescale_ =
      encoded_divisor < 1 || encoded_divisor != std::round(encoded_divisor);
  cleaning.input_level = cleaner_alignment_level_ - cleaner_alignment_rescale_;
  cleaning.normalized_input = true;
  cleaning.iteration_rescales = parameters.cleaning_rescales;
  cleaning.input_scale = parameters.input_scale;
  cleaning.extraction_scale = parameters.extraction_scale;
  cleaning.max_input_error = parameters.max_input_error;
  cleaning.max_digit_error = parameters.max_digit_error;
  cleaning.coefficients = parameters.extractor_coefficients;
  cleaning.extraction_baby_step = parameters.extraction_baby_step;
  auto cleaner = std::make_unique<DiscreteIntegerCleaner>(context, cleaning);
  // SplitRealImag doubles the channel; these constants restore integer units.
  context->encoder_.EncodeConstant(
      cleaner_offset_, cleaner_alignment_level_,
      2 * BootParameter::kModUpScale, 0.5);
  context->encoder_.EncodeConstant(
      cleaner_alignment_, cleaner_alignment_level_,
      (cleaner_alignment_rescale_ ?
           parameter.GetRescalePrimeProd(cleaner_alignment_level_) : 1) *
          alignment_scale, argument_gain);

  AssertTrue(cleaner->OutputLevel() > 0 &&
                 cleaner->OutputLevel() <= profile.OutputLevel(),
             "Integer cleaner cannot return above the accurate bypass");
  // The bypass returns (integer_units/8)*(I+m/g). Fuse conversion when possible.
  if (cleaner->OutputLevel() == profile.OutputLevel())
    transforms.BindBypassOutput(8 / integer_units, cleaner->OutputScale());
  const int alignment_level = cleaner->OutputLevel() + 1;
  context->encoder_.EncodeConstant(
      bypass_alignment_, alignment_level,
      parameter.GetRescalePrimeProd(alignment_level) *
          cleaner->OutputScale() / BootParameter::kModUpScale,
      8 / integer_units);
  const double normalization_scale =
      parameter.GetRescalePrimeProd(cleaner->OutputLevel()) *
          BootParameter::kApplicationScale / cleaner->OutputScale();
  AssertTrue(std::isfinite(gap_) && gap_ > 0 &&
                 std::isfinite(normalization_scale) &&
                 gap_ * normalization_scale >= std::exp2(20),
             "Normalization constant has insufficient encoded precision");
  context->encoder_.EncodeConstant(normalize_, cleaner->OutputLevel(),
                                   normalization_scale, gap_);
  output_level_ = cleaner->OutputLevel() - 1;
  output_scale_ = BootParameter::kApplicationScale;
  cleaner_ = std::move(cleaner);
}

void DiscreteIdentityExecution::CleanInteger(
    Ct &integer, Ct &&low, const EvkMap<uint64_t> &keys,
    const Observer &observe) const {
  CleanInteger(integer, std::move(low), keys, [&](Ct &channel) {
    auto context = context_.lock();
    AssertTrue(context != nullptr, "Discrete identity context expired");
    channel.SetScale(2 * channel.GetScale());
    context->DropLevel(channel, channel, cleaner_alignment_level_);
    context->Add(channel, channel, cleaner_offset_);
    Ct scaled;
    context->Mult(scaled, channel, cleaner_alignment_);
    if (cleaner_alignment_rescale_)
      context->Rescale(channel, scaled);
    else
      channel = std::move(scaled);
    context->AssertSameScale(channel, cleaner_input_scale_);
    channel.SetScale(cleaner_input_scale_);
  }, observe);
}

void DiscreteIdentityExecution::CleanInteger(
    Ct &integer, const Ct &low, const EvkMap<uint64_t> &keys,
    const Observer &observe) const {
  Ct copy;
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Discrete identity context expired");
  context->Copy(copy, low);
  CleanInteger(integer, std::move(copy), keys, observe);
}

void DiscreteIdentityExecution::CleanInteger(
    Ct &integer, Ct &&low, const EvkMap<uint64_t> &keys,
    const ChannelAligner &align, const Observer &observe) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Discrete identity context expired");
  Ct real, imaginary;
  context->SplitRealImag(real, imaginary, low, keys.GetConjugationKey());
  low = Ct();
  align(real);
  align(imaginary);
  if (!observe) {
    cleaner_->EvaluateBatch({&real, &imaginary}, {&real, &imaginary},
                            keys.GetMultiplicationKey(), "application");
    context->MultImaginaryUnit(imaginary, imaginary);
    context->Add(integer, real, imaginary);
    return;
  }
  auto clean = [&](Ct &channel, const char *name) {
    observe(name, channel);
    cleaner_->Evaluate(channel, channel, keys.GetMultiplicationKey(),
                       "application",
                       DiscreteIntegerCleaner::Observer(
                         [&](const char *stage, int bit, int iteration, const Ct &ct) {
                           const std::string label = iteration < 0 ?
                               std::string(name) + "/" + stage :
                               std::string(name) + "/" + stage + "/" +
                                   std::to_string(bit) + "/" +
                                   std::to_string(iteration);
                           observe(label.c_str(), ct);
                         }));
  };
  clean(real, "cleaner-input-real");
  clean(imaginary, "cleaner-input-imag");
  context->MultImaginaryUnit(imaginary, imaginary);
  context->Add(integer, real, imaginary);
  observe("clean-integer", integer);
}

void DiscreteIdentityExecution::CleanInteger(
    Ct &integer, const Ct &low, const EvkMap<uint64_t> &keys,
    const ChannelAligner &align, const Observer &observe) const {
  Ct copy;
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Discrete identity context expired");
  context->Copy(copy, low);
  CleanInteger(integer, std::move(copy), keys, align, observe);
}

void DiscreteIdentityExecution::AlignBypass(Ct &high, const Ct &bypass) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Discrete identity context expired");
  if (bypass.GetNP() == context->param_.LevelToNP(cleaner_->OutputLevel())) {
    context->AssertSameScale(bypass, cleaner_->OutputScale());
    context->Copy(high, bypass);
    return;
  }
  Ct lower, aligned;
  context->DropLevel(lower, bypass, output_level_ + 2);
  context->Mult(aligned, lower, bypass_alignment_);
  context->Rescale(high, aligned);
  context->AssertSameScale(high, cleaner_->OutputScale());
  high.SetScale(cleaner_->OutputScale());
}

void DiscreteIdentityExecution::Finish(
    Ct &result, const Ct &high, const Ct &integer,
    const Observer &observe) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Discrete identity context expired");
  context->AssertSameScale(high, cleaner_->OutputScale());
  Ct difference;
  context->Sub(difference, high, integer);
  context->Mult(difference, difference, normalize_);
  context->Rescale(result, difference);
  context->AssertSameScale(result, output_scale_);
  AssertTrue(result.GetNP() == context->param_.LevelToNP(output_level_),
             "Discrete identity output level contract mismatch");
  if (observe) observe("output", result);
}

DiscreteIdentityBaseline::DiscreteIdentityBaseline(
    ConstContextPtr<uint64_t> context,
    const std::vector<std::vector<double>> &extractor_coefficients)
    : DiscreteIdentityBaseline(
          context, DiscreteIdentityParameters::FromCoefficients(
                       5, extractor_coefficients, "discrete-baseline-hw30")) {}

DiscreteIdentityBaseline::DiscreteIdentityBaseline(
    ConstContextPtr<uint64_t> context,
    const DiscreteIdentityParameters &parameters)
    : context_(context) {
  parameters.Validate();
  const auto &parameter = context->param_;
  AssertTrue(parameters.width == 5 && parameters.input_scale == std::exp2(40) &&
                 parameters.extraction_scale == std::exp2(40),
             "Original Discrete identity requires the five-bit scale contract");
  AssertTrue(parameter.log_degree_ == 16 &&
                 parameter.max_key_switch_level_ == 31 &&
                 parameter.max_level_ == 31 &&
                 parameter.GetDenseHammingWeight() == 30 &&
                 parameter.GetSparseHammingWeight() == 30 &&
                 parameter.entry_primes_.empty() &&
                 parameter.ter_primes_.empty(),
             "Original Discrete profile requires full-ring HW30 and normal Q only");
  constexpr int slots = 32768;
  constexpr double stc_gain = 1.0 / 64;
  constexpr double stc_scale = double(uint64_t(1) << 36);
  integer_scale_ = double(parameter.main_primes_.front());
  const double gap = integer_scale_ / (stc_gain * stc_scale);
  auto stc = MEBootMatrixPlanner::GenerateElementaryStCStages(context, slots);
  auto cts = MEBootMatrixPlanner::GenerateElementaryCtSStages(context, slots);
  const std::vector<int> stc_widths{7, 8};
  int cursor = 0;
  for (int index = 0; index < 2; ++index) {
    const int level = 2 - index;
    const double input_scale = std::exp2(40);
    const double output_scale = index == 0 ? std::exp2(40) : stc_scale;
    auto matrix = Merge(stc, cursor, stc_widths[index], std::sqrt(stc_gain));
    const auto split = Transform::DefaultBSGSSplit(matrix.GetNumDiag() + (index == 0));
    const int pre = index == 0 ? -128 : 128;
    stc_.emplace_back(context, matrix, level,
                     parameter.GetRescalePrimeProd(level) * output_scale / input_scale,
                     split.first, split.second, pre, index == 0 ? 128 : 0);
    stc_.back().ReleaseCompactBackings();
    cursor += stc_widths[index];
  }
  DiscreteCleaningParameters cleaning;
  cleaning.input_level = 20;
  cleaning.width = parameters.width;
  cleaning.input_scale = parameters.input_scale;
  cleaning.extraction_scale = parameters.extraction_scale;
  cleaning.max_input_error = parameters.max_input_error;
  cleaning.max_digit_error = parameters.max_digit_error;
  cleaning.coefficients = parameters.extractor_coefficients;
  auto cleaner = std::make_unique<DiscreteIntegerCleaner>(
      context, std::move(cleaning));
  AssertTrue(cleaner->OutputLevel() == 9, "Unexpected original cleaner depth");
  // CtS's unnormalized butterfly product maps encoded slots to n times
  // coefficient order. Its gain 1/n restores I+m/g; split below divides by 2.
  const double cts_factor_gain = std::pow(1.0 / slots, 1.0 / 3);
  const std::vector<int> widths{5, 5, 5};
  auto prepare_cts = [&](std::vector<Transform> &factors, int start_level,
                         double final_scale) {
    const double scale_ratio = std::pow(final_scale / integer_scale_, 1.0 / 3);
    int begin = 0;
    for (int index = 0; index < 3; ++index) {
      const int level = start_level - index;
      auto matrix = Merge(cts, begin, widths[index], cts_factor_gain);
      const auto split = Transform::DefaultBSGSSplit(matrix.GetNumDiag() + (index == 2));
      const int remaining = 15 - begin - widths[index];
      const int pre = index == 0 ? (1 << remaining) :
                      index == 2 ? -(1 << widths[index]) :
                      -((1 << widths[index]) - 1) * (1 << remaining);
      factors.emplace_back(context, matrix, level,
                           parameter.GetRescalePrimeProd(level) * scale_ratio,
                           split.first, split.second, pre,
                           index == 2 ? 0 : -(1 << remaining));
      factors.back().ReleaseCompactBackings();
      begin += widths[index];
    }
  };
  prepare_cts(low_cts_, 31, std::exp2(39));
  const int high_start_level = 20;
  const int high_depth = high_start_level - cleaner->OutputLevel();
  const double high_factor_gain = std::pow(1.0 / slots, 1.0 / high_depth);
  const double high_scale_ratio = std::pow(cleaner->OutputScale() / integer_scale_, 1.0 / high_depth);
  int high_cursor = 0;
  int high_remaining = 15;
  for (int i = 0; i < high_depth; ++i) {
    const int width = (high_remaining + high_depth - i - 1) / (high_depth - i);
    high_remaining -= width;
    const int level = high_start_level - i;
    auto matrix = Merge(cts, high_cursor, width, high_factor_gain);
    int remaining_radix = 15 - high_cursor - width;
    auto [bs, gs] = Transform::DefaultBSGSSplit(matrix.GetNumDiag() + (i == high_depth - 1));
    const int pre = i == 0 ? (1 << remaining_radix) :
                    i == high_depth - 1 ? -(1 << width) :
                    -((1 << width) - 1) * (1 << remaining_radix);
    const int additional = i == high_depth - 1 ? 0 : -(1 << remaining_radix);
    high_cts_.emplace_back(context, matrix, level,
                          parameter.GetRescalePrimeProd(level) * high_scale_ratio,
                          bs, gs, pre, additional);
    high_cts_.back().ReleaseCompactBackings();
    high_cursor += width;
  }
  // This physical multiplication and rescale restores message units and the
  // application scale. It consumes the recorded final main-chain prime.
  const int normalize_level = cleaner->OutputLevel();
  const double constant_scale = parameter.GetRescalePrimeProd(normalize_level) *
                                std::exp2(40) / cleaner->OutputScale();
  Constant<uint64_t> normalize;
  context->encoder_.EncodeConstant(normalize, normalize_level,
                                   constant_scale, gap);
  AssertTrue(gap * constant_scale >= std::exp2(20),
             "Normalization constant has insufficient encoded precision");
  execution_ = std::make_unique<DiscreteIdentityExecution>(
      context, std::move(cleaner), Constant<uint64_t>{},
      std::move(normalize), gap, normalize_level - 1, std::exp2(40));
}

void DiscreteIdentityBaseline::AddRequiredRotations(EvkRequest &request) const {
  for (const auto *group : {&stc_, &low_cts_, &high_cts_})
    for (const auto &factor : *group) factor.AddRequiredRotations(request);
}

void DiscreteIdentityBaseline::Lift(Ct &result, const Ct &input) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr && input.GetNP() == context->param_.LevelToNP(0),
             "Original Discrete lift requires coefficient Q0 input");
  const auto target = context->param_.LevelToNP(31);
  Ct output(target);
  output.SetScale(integer_scale_);
  output.SetNumSlots(input.GetNumSlots());
  for (int component = 0; component < 2; ++component) {
    DeviceVector<uint64_t> coefficients(context->param_.degree_);
    auto view = coefficients.View();
    const auto source = component ? input.AxConstView() : input.BxConstView();
    context->ntt_handler_.INTT(view, input.GetNP(), source, true);
    auto destination = component ? output.AxView() : output.BxView();
    context->elem_handler_.ModUpToMax(destination, coefficients.ConstView(), target);
    const auto lifted = component ? output.AxConstView() : output.BxConstView();
    context->ntt_handler_.NTT(destination, target, lifted, true);
  }
  result = std::move(output);
}

void DiscreteIdentityBaseline::Evaluate(Ct &result, const Ct &input,
                                       const EvkMap<uint64_t> &keys,
                                       const Observer &observe) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr && input.GetNP() == context->param_.LevelToNP(2) &&
                 input.GetNumSlots() == 32768,
             "Original Discrete requires full-slot level-2 input");
  context->AssertSameScale(input, std::exp2(40));
  Ct coefficient, raised, dense;
  Apply(context, coefficient, input, keys, stc_);
  if (observe) observe("stc", coefficient);
  if (context->param_.IsUsingSparseSecretEncapsulation()) {
    context->MultKey(coefficient, coefficient, keys.GetDenseToSparseKey());
  }
  Lift(raised, coefficient);
  if (context->param_.IsUsingSparseSecretEncapsulation()) {
    context->MultKey(dense, raised, keys.GetSparseToDenseKey());
  } else {
    dense = std::move(raised);
  }
  if (observe) observe("raised", dense);
  Ct low;
  Apply(context, low, dense, keys, low_cts_);
  if (observe) observe("low-cts", low);
  context->DropLevel(low, low, 20);
  Ct integer;
  execution_->CleanInteger(
      integer, std::move(low), keys,
      [&](Ct &channel) {
        channel.SetScale(channel.GetScale() * 2);
        context->AssertSameScale(channel, std::exp2(40));
        channel.SetScale(std::exp2(40));
      },
      observe);
  Ct high_input, high;
  context->DropLevel(high_input, dense, 20);
  Apply(context, high, high_input, keys, high_cts_);
  if (observe) observe("accurate-bypass", high);
  execution_->Finish(result, high, integer, observe);
}

DiscreteIdentityTiming DiscreteIdentityBaseline::Profile(
    Ct &result, const Ct &input, const EvkMap<uint64_t> &keys) const {
  return ProfileEvaluate([&](const Observer &observe) {
    Evaluate(result, input, keys, observe);
  });
}

MEBOOTDiscreteIdentity::MEBOOTDiscreteIdentity(
    ConstContextPtr<uint64_t> context, const BootParameter &profile,
    const std::vector<std::vector<double>> &extractor_coefficients)
    : MEBOOTDiscreteIdentity(
          context, profile,
          DiscreteIdentityParameters::FromCoefficients(
              6, extractor_coefficients, "meboot-discrete-identity-hw40")) {}

MEBOOTDiscreteIdentity::MEBOOTDiscreteIdentity(
    ConstContextPtr<uint64_t> context, const BootParameter &profile,
    const DiscreteIdentityParameters &parameters)
    : context_(context), transforms_(context, profile) {
  parameters.Validate();
  const auto &parameter = context->param_;
  AssertTrue(profile.route == BootRoute::kDiscreteCKKSMeBoot &&
                 parameter.main_primes_ == profile.q &&
                 parameter.aux_primes_ == profile.p &&
                 parameter.ter_primes_ == std::vector<uint64_t>{profile.extra_q} &&
                 parameter.entry_primes_ == std::vector<uint64_t>{786433} &&
                 parameter.GetDenseHammingWeight() == 2048 &&
                 parameter.GetSparseHammingWeight() == 40 &&
                 profile.extension == 8 && context->entry_gadget_ != nullptr &&
                 parameters.width == 6,
             "MEBOOT Discrete requires the unchanged Identity19 HW40 entry profile");
  profile.ValidateScalingBinding();
  constexpr int slots = BootParameter::kSlots;
  const double integer_units = double(profile.q[0]) * profile.ModUpMultiplier() /
                               BootParameter::kModUpScale;
  const double cts_gain = 1.0 / (slots * integer_units);
  low_cts_ = std::make_unique<MEBootCtS<uint64_t>>(
      context, slots, 32, cts_gain, profile.widths, profile.bases, profile.extension);
  VerifyMEBootLCRBounds(context, *low_cts_, uint64_t(profile.ModUpMultiplier()));

  execution_ = std::make_unique<DiscreteIdentityExecution>(
      context, profile, parameters, transforms_);
}

void MEBOOTDiscreteIdentity::AddRequiredRotations(EvkRequest &request) const {
  transforms_.AddRequiredRotations(request);
  low_cts_->AddRequiredRotations(request);
}

void MEBOOTDiscreteIdentity::Evaluate(
    Ct &result, const Ct &input, const EvkMap<uint64_t> &keys,
    const StageMarker &mark, const Observer &observe) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr && input.GetNP() == context->param_.LevelToNP(2) &&
                 input.GetNumSlots() == BootParameter::kSlots,
             "MEBOOT Discrete requires full-slot level-2 input");
  context->AssertSameScale(input, std::exp2(40));
  Ct raised, dense;
  transforms_.Enter(raised, dense, input, keys, mark, observe);
  Ct low;
  low_cts_->Evaluate(low, raised, keys);
  raised = Ct();
  if (mark) mark("LowCtS");
  if (observe) observe("low-cts", low);
  if (observe) {
    Ct integer;
    execution_->CleanInteger(integer, std::move(low), keys, observe);
    low = Ct();
    Ct bypass, high;
    transforms_.Bypass(bypass, dense, keys);
    dense = Ct();
    execution_->AlignBypass(high, bypass);
    bypass = Ct();
    if (mark) mark("AccurateBypass");
    observe("accurate-bypass", high);
    execution_->Finish(result, high, integer, observe);
    high = Ct();
    integer = Ct();
    if (mark) mark("DiscreteFinish");
  } else {
    Ct bypass, high;
    transforms_.Bypass(bypass, dense, keys);
    dense = Ct();
    execution_->AlignBypass(high, bypass);
    bypass = Ct();
    Ct integer;
    execution_->CleanInteger(integer, std::move(low), keys, observe);
    low = Ct();
    if (mark) mark("AccurateBypass");
    execution_->Finish(result, high, integer, observe);
    high = Ct();
    integer = Ct();
    if (mark) mark("DiscreteFinish");
  }
}

DiscreteIdentityTiming MEBOOTDiscreteIdentity::Profile(
    Ct &result, const Ct &input, const EvkMap<uint64_t> &keys) const {
  return ProfileEvaluate([&](const Observer &observe) {
    Evaluate(result, input, keys, {}, observe);
  });
}

}  // namespace cheddar
