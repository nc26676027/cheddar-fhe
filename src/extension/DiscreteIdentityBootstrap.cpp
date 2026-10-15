#include "extension/DiscreteIdentityBootstrap.h"

#include <cmath>
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
}  // namespace

DiscreteIdentityBaseline::DiscreteIdentityBaseline(
    ConstContextPtr<uint64_t> context,
    const std::vector<std::vector<double>> &extractor_coefficients)
    : context_(context) {
  const auto &parameter = context->param_;
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
  gap_ = integer_scale_ / (stc_gain * stc_scale);
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
  cleaning.input_scale = std::exp2(40);
  cleaning.extraction_scale = std::exp2(40);
  cleaning.max_input_error = std::exp2(-12);
  cleaning.max_digit_error = std::exp2(-9);
  cleaning.coefficients = extractor_coefficients;
  cleaner_ = std::make_unique<DiscreteIntegerCleaner>(context, std::move(cleaning));
  AssertTrue(cleaner_->OutputLevel() == 9, "Unexpected original cleaner depth");
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
  const int high_depth = high_start_level - cleaner_->OutputLevel();
  const double high_factor_gain = std::pow(1.0 / slots, 1.0 / high_depth);
  const double high_scale_ratio = std::pow(cleaner_->OutputScale() / integer_scale_, 1.0 / high_depth);
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
  const int normalize_level = cleaner_->OutputLevel();
  const double constant_scale = parameter.GetRescalePrimeProd(normalize_level) *
                                OutputScale() / cleaner_->OutputScale();
  context->encoder_.EncodeConstant(normalize_, normalize_level,
                                   constant_scale, gap_);
  AssertTrue(gap_ * constant_scale >= std::exp2(20),
             "Normalization constant has insufficient encoded precision");
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
  Ct real, imaginary;
  context->SplitRealImag(real, imaginary, low, keys.GetConjugationKey());
  // Native split returns 2 Re and 2 Im. Restore integer units explicitly.
  real.SetScale(real.GetScale() * 2);
  imaginary.SetScale(imaginary.GetScale() * 2);
  // The transform compiler's accumulated double ratio is checked before
  // adopting the frozen input scale used by the shared cleaner.
  context->AssertSameScale(real, std::exp2(40));
  real.SetScale(std::exp2(40));
  imaginary.SetScale(std::exp2(40));
  Ct clean_real, clean_imaginary;
  cleaner_->Evaluate(clean_real, real, keys.GetMultiplicationKey(), "application");
  cleaner_->Evaluate(clean_imaginary, imaginary, keys.GetMultiplicationKey(), "application");
  context->MultImaginaryUnit(clean_imaginary, clean_imaginary);
  Ct integer;
  context->Add(integer, clean_real, clean_imaginary);
  Ct high_input, high;
  context->DropLevel(high_input, dense, 20);
  Apply(context, high, high_input, keys, high_cts_);
  if (observe) { observe("high-cts", high); observe("clean-integer", integer); }
  context->AssertSameScale(high, cleaner_->OutputScale());
  high.SetScale(cleaner_->OutputScale());
  Ct difference;
  context->Sub(difference, high, integer);
  Ct normalized;
  context->Mult(normalized, difference, normalize_);
  context->Rescale(result, normalized);
  context->AssertSameScale(result, OutputScale());
  if (observe) observe("output", result);
}

}  // namespace cheddar
