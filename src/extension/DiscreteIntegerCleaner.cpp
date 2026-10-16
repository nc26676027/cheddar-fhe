#include "extension/DiscreteIntegerCleaner.h"

#include <cmath>
#include "common/Assert.h"
#include "common/PrimeUtils.h"

namespace cheddar {
namespace {
// Encode the integer 3*D exactly even if that product exceeds 53 bits.
void EncodeThreeScale(ConstContextPtr<uint64_t> context,
                      Constant<uint64_t> &constant, int level, double scale) {
  AssertTrue(std::isfinite(scale) && scale >= 1 && scale < std::ldexp(1.0, 63) &&
                 std::floor(scale) == scale,
             "Thrifty scale needs a representable positive integer below 2^63");
  const auto np = context->param_.LevelToNP(level);
  const auto primes = context->param_.GetPrimeVector(np);
  HostVector<uint64_t> residues(primes.size());
  for (size_t index = 0; index < primes.size(); ++index) {
    const auto prime = primes[index];
    const uint64_t residue = uint64_t(
        (static_cast<unsigned __int128>(uint64_t(scale)) * 3) % prime);
    residues[index] = primeutil::ToMontgomery(residue, prime);
  }
  constant.ModifyNP(np);
  constant.SetScale(scale);
  CopyHostToDevice(constant.cx_, residues);
}
}  // namespace

DiscreteIntegerCleaner::DiscreteIntegerCleaner(
    ConstContextPtr<uint64_t> context, DiscreteCleaningParameters parameters)
    : context_(context), parameters_(std::move(parameters)) {
  const auto &p = parameters_;
  AssertTrue(p.width >= 2 && p.width <= 6 &&
                 int(p.coefficients.size()) == p.width &&
                 p.input_scale > 0 && std::isfinite(p.input_scale) &&
                 p.extraction_scale >= 1 && std::isfinite(p.extraction_scale) &&
                 p.max_input_error > 0 && p.max_input_error <= std::exp2(-12) &&
                 p.max_digit_error > 0 && p.max_digit_error <= std::exp2(-9) &&
                 p.iterations > 0 && p.iterations <= 3 &&
                 p.final_rescales >= 0 && !p.secret_domain.empty(),
             "Invalid discrete cleaning contract");
  if (parameters_.iteration_rescales.empty())
    parameters_.iteration_rescales.assign(p.iterations, 2);
  AssertTrue(int(p.iteration_rescales.size()) == p.iterations,
             "Cleaning needs one rescale count per iteration");
  int cleaning_rescales = 0;
  for (int count : p.iteration_rescales) {
    AssertTrue(count >= 1 && count <= 2, "Invalid cubic rescale count");
    cleaning_rescales += count;
  }
  auto odd = p.coefficients;
  for (auto &polynomial : odd) {
    AssertTrue(!polynomial.empty() && polynomial[0] == 0.5,
               "Extractor requires offset-binary half-offset polynomials");
    polynomial[0] = 0;
    for (size_t degree = 2; degree < polynomial.size(); degree += 2)
      AssertTrue(polynomial[degree] == 0, "Extractor requires odd remainder");
  }
  const double divisor = double(uint64_t(1) << (p.width - 1));
  const int extraction_input_level = p.input_level - !p.normalized_input;
  if (!p.normalized_input) {
    const double q_top = context->param_.GetRescalePrimeProd(p.input_level);
    context->encoder_.EncodeConstant(divisor_constant_, p.input_level, q_top,
                                     1.0 / divisor);
    context->encoder_.EncodeConstant(input_offset_, p.input_level, p.input_scale, 0.5);
  }

  extractor_ = std::make_unique<EvalPolyBatch<uint64_t>>(
      odd, extraction_input_level, p.input_scale, p.extraction_scale, true,
      p.extraction_baby_step);
  const int extraction_level = extraction_input_level - extractor_->GetDepth();
  AssertTrue(extraction_level >= cleaning_rescales + p.final_rescales &&
                 p.input_level <= context->param_.max_key_switch_level_,
             "Insufficient normal-chain modulus for integer cleaning");
  extractor_->Compile(context);
  context->encoder_.EncodeConstant(digit_offset_, extraction_level,
                                   p.extraction_scale, 0.5);
  double scale = p.extraction_scale;
  cubic_square_.resize(p.iterations);
  cubic_cube_.resize(p.iterations);
  int level = extraction_level;
  for (int iteration = 0; iteration < p.iterations; ++iteration) {
    const double rounded = std::round(scale);
    scale_reinterpretation_errors_.push_back(std::abs(scale / rounded - 1));
    scale = rounded;
    rounded_scales_.push_back(scale);
    EncodeThreeScale(context, cubic_square_[iteration], level, scale);
    context->encoder_.EncodeConstant(cubic_cube_[iteration], level, 1.0, 2.0);
    scale = scale * scale * scale;
    for (int index = 0; index < p.iteration_rescales[iteration]; ++index)
      scale /= context->param_.GetRescalePrimeProd(level--);
    AssertTrue(std::isfinite(scale) && scale > 0, "Invalid cleaning output scale");
    iteration_scales_.push_back(scale);
  }
  for (int index = 0; index < p.final_rescales; ++index)
    scale /= context->param_.GetRescalePrimeProd(level--);
  output_scale_ = scale;
  const int output_level = OutputLevel();
  weights_.resize(p.width);
  for (int bit = 0; bit < p.width; ++bit)
    context->encoder_.EncodeConstant(weights_[bit], output_level, 1.0, double(1 << bit));
  context->encoder_.EncodeConstant(signed_offset_, output_level,
                                   output_scale_, divisor);
}

int DiscreteIntegerCleaner::OutputLevel() const {
  int level = parameters_.input_level - !parameters_.normalized_input -
              extractor_->GetDepth() - parameters_.final_rescales;
  for (int count : parameters_.iteration_rescales) level -= count;
  return level;
}

DiscreteCleaningState DiscreteIntegerCleaner::Evaluate(
    Ct &result, const Ct &input, const EvaluationKey<uint64_t> &mult_key,
    const std::string &secret_domain, const Observer &observe) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Discrete cleaner context expired");
  const auto &p = parameters_;
  AssertTrue(secret_domain == p.secret_domain, "Discrete cleaner secret domain mismatch");
  AssertTrue(input.GetNP() == context->param_.LevelToNP(p.input_level) &&
                 !input.HasRx(), "Discrete cleaner input basis mismatch");
  context->AssertSameScale(input, p.input_scale);
  Ct argument;
  if (p.normalized_input) {
    context->Copy(argument, input);
  } else {
    Ct centered, scaled_arg;
    context->Add(centered, input, input_offset_);
    context->Mult(scaled_arg, centered, divisor_constant_);
    context->Rescale(argument, scaled_arg);
  }
  context->AssertSameScale(argument, p.input_scale);

  std::vector<Ct> digits;
  extractor_->Evaluate(context, digits, argument, mult_key);
  // The input perturbation is part of the integer contract: extraction
  // error is additive to the declared input radius before cleaning.
  double ideal_error = p.max_input_error + p.max_digit_error;
  for (int bit = 0; bit < p.width; ++bit) {
    context->Add(digits[bit], digits[bit], digit_offset_);
    if (observe) observe("extract", bit, 0, digits[bit]);
    for (int iteration = 0; iteration < p.iterations; ++iteration) {
      Ct square, cube, scaled_square, twice_cube, polynomial;
      // Sec.4.1 permits replacing Delta by its nearest integer; the changed
      // represented value and its error must be included in encrypted checks.
      digits[bit].SetScale(rounded_scales_[iteration]);
      context->Mult(square, digits[bit], digits[bit]);
      context->Relinearize(square, square, mult_key);
      context->Mult(cube, digits[bit], square);
      context->Relinearize(cube, cube, mult_key);
      context->Mult(scaled_square, square, cubic_square_[iteration]);
      context->Mult(twice_cube, cube, cubic_cube_[iteration]);
      context->Sub(polynomial, scaled_square, twice_cube);
      for (int index = 0; index < p.iteration_rescales[iteration]; ++index) {
        Ct lower;
        context->Rescale(lower, polynomial);
        polynomial = std::move(lower);
      }
      digits[bit] = std::move(polynomial);
      context->AssertSameScale(digits[bit], iteration_scales_[iteration]);
      if (observe) observe("clean", bit, iteration + 1, digits[bit]);
    }
    for (int index = 0; index < p.final_rescales; ++index) {
      Ct lower;
      context->Rescale(lower, digits[bit]);
      digits[bit] = std::move(lower);
    }
  }
  for (int iteration = 0; iteration < p.iterations; ++iteration) {
    ideal_error += scale_reinterpretation_errors_[iteration] * (1 + ideal_error);
    ideal_error = (3 + 2 * ideal_error) * ideal_error * ideal_error;
  }
  Ct accumulator;
  context->Mult(accumulator, digits.front(), weights_.front());
  for (int bit = 1; bit < p.width; ++bit) {
    Ct weighted;
    context->Mult(weighted, digits[bit], weights_[bit]);
    context->Add(accumulator, accumulator, weighted);
  }
  Ct output;
  context->Sub(output, accumulator, signed_offset_);
  result = std::move(output);
  if (observe) observe("combine", -1, 0, result);
  return {secret_domain, OutputLevel(), output_scale_, p.width,
          extractor_->GetDepth(), 2 * p.iterations,
          p.input_level - OutputLevel(),
          double((1 << p.width) - 1) * ideal_error};
}
}  // namespace cheddar
