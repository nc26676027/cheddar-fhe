#pragma once

#include <functional>
#include <string>
#include "extension/EvalPoly.h"

namespace cheddar {

struct DiscreteCleaningParameters {
  int width = 5;
  int input_level = 0;
  double input_scale = 0;
  // Polynomial output scale; independent of the shared power-basis input scale.
  double extraction_scale = 0;
  int extraction_baby_step = 0;
  double max_input_error = 0;
  double max_digit_error = 0;
  int iterations = 2;
  int final_rescales = 0;
  // The adapter may supply the polynomial argument using its certified map.
  bool normalized_input = false;
  std::vector<int> iteration_rescales;
  std::string secret_domain = "application";
  std::vector<std::vector<double>> coefficients;
};

struct DiscreteCleaningState {
  std::string secret_domain;
  int level = 0;
  double scale = 0;
  int width = 0;
  int extraction_depth = 0;
  int cleaning_multiplicative_depth = 0;
  int rescale_count = 0;
  double ideal_integer_error_bound = 0;
};

// Output is in integer units; input is either in integer units or explicitly
// normalized as declared above. The caller binds the secret.
// Ideal bounds exclude encrypted evaluation noise, which needs its own ledger.
class DiscreteIntegerCleaner {
 public:
  using Ct = Ciphertext<uint64_t>;
  using Observer = std::function<void(const char *, int, int, const Ct &)>;
  DiscreteIntegerCleaner(ConstContextPtr<uint64_t> context,
                         DiscreteCleaningParameters parameters);
  DiscreteCleaningState Evaluate(
      Ct &result, const Ct &input, const EvaluationKey<uint64_t> &mult_key,
      const std::string &secret_domain, const Observer &observe = {}) const;
  DiscreteCleaningState EvaluateBatch(
      const std::vector<Ct *> &results,
      const std::vector<const Ct *> &inputs,
      const EvaluationKey<uint64_t> &mult_key,
      const std::string &secret_domain) const;
  int OutputLevel() const;
  double OutputScale() const { return output_scale_; }

 private:
  void CleanDigitsBatch(
      ConstContextPtr<uint64_t> context,
      const std::vector<Ct *> &digits,
      const EvaluationKey<uint64_t> &mult_key,
      size_t chunk_size,
      const std::function<void(int bit, int iteration, const Ct &)> &observe = nullptr) const;

  std::weak_ptr<const Context<uint64_t>> context_;
  DiscreteCleaningParameters parameters_;
  std::unique_ptr<EvalPolyBatch<uint64_t>> extractor_;
  Constant<uint64_t> divisor_constant_, input_offset_, digit_offset_, signed_offset_;
  std::vector<Constant<uint64_t>> cubic_offset_, cubic_slope_, weights_;
  std::vector<double> rounded_scales_, iteration_scales_;
  std::vector<double> scale_reinterpretation_errors_;
  double output_scale_ = 0;
};

}  // namespace cheddar
