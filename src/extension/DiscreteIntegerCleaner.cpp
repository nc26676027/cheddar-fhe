#include "extension/DiscreteIntegerCleaner.h"

#include <algorithm>
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
  cubic_offset_.resize(p.iterations);
  cubic_slope_.resize(p.iterations);
  int level = extraction_level;
  for (int iteration = 0; iteration < p.iterations; ++iteration) {
    const double rounded = std::round(scale);
    scale_reinterpretation_errors_.push_back(std::abs(scale / rounded - 1));
    scale = rounded;
    rounded_scales_.push_back(scale);
    EncodeThreeScale(context, cubic_offset_[iteration], level, scale);
    context->encoder_.EncodeConstant(cubic_slope_[iteration], level, 1.0, 2.0);
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
  context->encoder_.EncodeConstant(neg_signed_offset_, output_level,
                                   output_scale_, -divisor);
}

int DiscreteIntegerCleaner::OutputLevel() const {
  int level = parameters_.input_level - !parameters_.normalized_input -
              extractor_->GetDepth() - parameters_.final_rescales;
  for (int count : parameters_.iteration_rescales) level -= count;
  return level;
}

DiscreteCleaningState DiscreteIntegerCleaner::EvaluateBatch(
    const std::vector<Ct *> &results, const std::vector<const Ct *> &inputs,
    const EvaluationKey<uint64_t> &mult_key,
    const std::string &secret_domain) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Discrete cleaner context expired");
  const auto &p = parameters_;
  AssertTrue(secret_domain == p.secret_domain,
             "Discrete cleaner secret domain mismatch");
  AssertTrue(results.size() == inputs.size() && !inputs.empty(),
             "Discrete cleaner batch size mismatch");
  const size_t num_channels = inputs.size();
  std::vector<Ct> arguments;
  std::vector<const Ct *> arg_ptrs(num_channels);
  if (!p.normalized_input) {
    arguments.resize(num_channels);
  }
  for (size_t c = 0; c < num_channels; ++c) {
    const Ct &input = *inputs[c];
    AssertTrue(input.GetNP() == context->param_.LevelToNP(p.input_level) &&
                   !input.HasRx(),
               "Discrete cleaner input basis mismatch");
    context->AssertSameScale(input, p.input_scale);
    if (p.normalized_input) {
      arg_ptrs[c] = &input;
    } else {
      Ct centered, scaled_arg;
      context->Add(centered, input, input_offset_);
      context->Mult(scaled_arg, centered, divisor_constant_);
      context->Rescale(arguments[c], scaled_arg);
      context->AssertSameScale(arguments[c], p.input_scale);
      arg_ptrs[c] = &arguments[c];
    }
  }

  std::vector<std::vector<Ct>> digits_batch(num_channels);
  for (size_t c = 0; c < num_channels; ++c) {
    extractor_->Evaluate(context, digits_batch[c], *arg_ptrs[c], mult_key);
    if (results[c] == inputs[c]) {
      *results[c] = Ct();
    }
  }
  arguments.clear();

  const size_t total_digits = num_channels * static_cast<size_t>(p.width);
  std::vector<Ct *> all_digits;
  all_digits.reserve(total_digits);
  for (size_t c = 0; c < num_channels; ++c) {
    for (int bit = 0; bit < p.width; ++bit) {
      context->Add(digits_batch[c][bit], digits_batch[c][bit], digit_offset_);
      all_digits.push_back(&digits_batch[c][bit]);
    }
  }

  CleanDigitsBatch(context, all_digits, mult_key, 6, nullptr);

  double ideal_error = p.max_input_error + p.max_digit_error;
  for (int iteration = 0; iteration < p.iterations; ++iteration) {
    ideal_error += scale_reinterpretation_errors_[iteration] * (1 + ideal_error);
    ideal_error = (3 + 2 * ideal_error) * ideal_error * ideal_error;
  }

  const int output_level = OutputLevel();
  const NPInfo output_np = context->param_.LevelToNP(output_level);
  for (size_t c = 0; c < num_channels; ++c) {
    auto &digits = digits_batch[c];
    Ct output;
    output.ModifyNP(output_np);
    output.SetNumSlots(digits.front().GetNumSlots());
    output.SetScale(output_scale_);

    std::vector<std::vector<DvConstView<uint64_t>>> shared_ct_srcs(p.width);
    for (int bit = 0; bit < p.width; ++bit) {
      shared_ct_srcs[bit] = digits[bit].ConstViewVector(0, true);
    }
    std::vector<std::vector<DvConstView<uint64_t>>> const_srcs_batch(1);
    const_srcs_batch[0].reserve(p.width);
    for (int bit = 0; bit < p.width; ++bit) {
      const_srcs_batch[0].push_back(weights_[bit].ConstView());
    }
    std::vector<DvConstView<uint64_t>> c0_consts_batch = {
        neg_signed_offset_.ConstView()};
    std::vector<std::vector<DvView<uint64_t>>> dst_batch = {
        output.ViewVector(0, true)};

    context->elem_handler_.MultiCAccum(dst_batch, output_np, shared_ct_srcs,
                                       const_srcs_batch, {}, c0_consts_batch);
    *results[c] = std::move(output);
  }

  return {secret_domain, OutputLevel(), output_scale_, p.width,
          extractor_->GetDepth(), 2 * p.iterations,
          p.input_level - OutputLevel(),
          double((1 << p.width) - 1) * ideal_error};
}

DiscreteCleaningState DiscreteIntegerCleaner::Evaluate(
    Ct &result, const Ct &input, const EvaluationKey<uint64_t> &mult_key,
    const std::string &secret_domain, const Observer &observe) const {
  if (!observe) {
    return EvaluateBatch({&result}, {&input}, mult_key, secret_domain);
  }
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
  extractor_->Evaluate(
      context, digits, argument, mult_key,
      EvalPolyBatch<uint64_t>::Observer(
          [&](const char *, int index, const Ct &ct) {
            if (index < 0) {
              observe("extract/basis", -1, -1, ct);
            } else {
              const std::string label =
                  "extract/tree/" + std::to_string(index);
              observe(label.c_str(), -1, -1, ct);
            }
          }));
  // The input perturbation is part of the integer contract: extraction
  // error is additive to the declared input radius before cleaning.
  double ideal_error = p.max_input_error + p.max_digit_error;
  std::vector<Ct *> digit_ptrs(p.width);
  for (int bit = 0; bit < p.width; ++bit) {
    context->Add(digits[bit], digits[bit], digit_offset_);
    digit_ptrs[bit] = &digits[bit];
    observe("extract", bit, 0, digits[bit]);
  }
  CleanDigitsBatch(context, digit_ptrs, mult_key, p.width,
                   [&](int bit, int iteration, const Ct &ct) {
                     observe("clean", bit, iteration, ct);
                   });
  for (int iteration = 0; iteration < p.iterations; ++iteration) {
    ideal_error += scale_reinterpretation_errors_[iteration] * (1 + ideal_error);
    ideal_error = (3 + 2 * ideal_error) * ideal_error * ideal_error;
  }
  const int output_level = OutputLevel();
  const NPInfo output_np = context->param_.LevelToNP(output_level);
  Ct output;
  output.ModifyNP(output_np);
  output.SetNumSlots(digits.front().GetNumSlots());
  output.SetScale(output_scale_);

  std::vector<std::vector<DvConstView<uint64_t>>> shared_ct_srcs(p.width);
  for (int bit = 0; bit < p.width; ++bit) {
    shared_ct_srcs[bit] = digits[bit].ConstViewVector(0, true);
  }
  std::vector<std::vector<DvConstView<uint64_t>>> const_srcs_batch(1);
  const_srcs_batch[0].reserve(p.width);
  for (int bit = 0; bit < p.width; ++bit) {
    const_srcs_batch[0].push_back(weights_[bit].ConstView());
  }
  std::vector<DvConstView<uint64_t>> c0_consts_batch = {
      neg_signed_offset_.ConstView()};
  std::vector<std::vector<DvView<uint64_t>>> dst_batch = {
      output.ViewVector(0, true)};

  context->elem_handler_.MultiCAccum(dst_batch, output_np, shared_ct_srcs,
                                     const_srcs_batch, {}, c0_consts_batch);
  result = std::move(output);
  observe("combine", -1, 0, result);
  return {secret_domain, OutputLevel(), output_scale_, p.width,
          extractor_->GetDepth(), 2 * p.iterations,
          p.input_level - OutputLevel(),
          double((1 << p.width) - 1) * ideal_error};
}

void DiscreteIntegerCleaner::CleanDigitsBatch(
    ConstContextPtr<uint64_t> context,
    const std::vector<Ct *> &digits,
    const EvaluationKey<uint64_t> &mult_key,
    size_t chunk_size,
    const std::function<void(int bit, int iteration, const Ct &)> &observe) const {
  const auto &p = parameters_;
  const size_t total_digits = digits.size();
  for (int iteration = 0; iteration < p.iterations; ++iteration) {
    for (size_t offset = 0; offset < total_digits; offset += chunk_size) {
      const size_t B = std::min(chunk_size, total_digits - offset);
      std::vector<Ct> squares(B);
      std::vector<Ct *> sq_ptrs(B);
      std::vector<const Ct *> sq_const_ptrs(B);
      for (size_t b = 0; b < B; ++b) {
        size_t idx = offset + b;
        digits[idx]->SetScale(rounded_scales_[iteration]);
        context->Mult(squares[b], *digits[idx], *digits[idx]);
        sq_ptrs[b] = &squares[b];
        sq_const_ptrs[b] = &squares[b];
      }
      context->RelinearizeBatch(sq_ptrs, sq_const_ptrs, mult_key);

      std::vector<Ct> polynomials(B);
      std::vector<const Ct *> poly_const_ptrs(B);
      for (size_t b = 0; b < B; ++b) {
        size_t idx = offset + b;
        Ct &poly = polynomials[b];
        Ct &sq = squares[b];
        Ct &digit = *digits[idx];
        poly.ModifyNP(sq.GetNP());
        poly.PrepareRx();
        poly.SetNumSlots(std::max(sq.GetNumSlots(), digit.GetNumSlots()));
        poly.SetScale(sq.GetScale() * cubic_offset_[iteration].GetScale());
        auto poly_views = poly.ViewVector();
        context->elem_handler_.TensorLinearFactor(
            poly_views, poly.GetNP(), sq.ConstViewVector(0, true),
            digit.ConstViewVector(0, true),
            cubic_slope_[iteration].ConstView(),
            cubic_offset_[iteration].ConstView());
        sq = Ct();
        poly_const_ptrs[b] = &poly;
      }
      squares.clear();

      std::vector<Ct *> chunk_digits(digits.begin() + offset,
                                     digits.begin() + offset + B);
      context->RelinearizeRescaleBatch(chunk_digits, poly_const_ptrs, mult_key);
      polynomials.clear();
    }

    std::vector<const Ct *> const_digits(total_digits);
    for (size_t idx = 0; idx < total_digits; ++idx) {
      const_digits[idx] = digits[idx];
    }
    for (int index = 1; index < p.iteration_rescales[iteration]; ++index) {
      context->RescaleBatch(digits, const_digits);
    }
    for (size_t idx = 0; idx < total_digits; ++idx) {
      context->AssertSameScale(*digits[idx], iteration_scales_[iteration]);
      if (observe) {
        observe(static_cast<int>(idx), iteration + 1, *digits[idx]);
      }
    }
  }

  std::vector<const Ct *> const_digits(total_digits);
  for (size_t idx = 0; idx < total_digits; ++idx) {
    const_digits[idx] = digits[idx];
  }
  for (int index = 0; index < p.final_rescales; ++index) {
    context->RescaleBatch(digits, const_digits);
  }
}

}  // namespace cheddar
