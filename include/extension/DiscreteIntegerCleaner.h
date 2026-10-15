#pragma once

#include <functional>
#include <string>
#include "extension/EvalPoly.h"

namespace cheddar {

struct DiscreteCleaningParameters {
  int width = 5;
  int input_level = 0;
  double input_scale = 0;
  double extraction_scale = 0;
  double max_input_error = 0;
  double max_digit_error = 0;
  int iterations = 2;
  int final_rescales = 0;
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

// Input/output are real slots in integer units; the caller binds the secret.
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
  int OutputLevel() const;
  double OutputScale() const { return output_scale_; }

 private:
  std::weak_ptr<const Context<uint64_t>> context_;
  DiscreteCleaningParameters parameters_;
  std::unique_ptr<EvalPolyBatch<uint64_t>> extractor_;
  Constant<uint64_t> input_offset_, digit_offset_, signed_offset_;
  std::vector<Constant<uint64_t>> cubic_square_, cubic_cube_, weights_;
  std::vector<double> rounded_scales_, iteration_scales_;
  std::vector<double> scale_reinterpretation_errors_;
  double output_scale_ = 0;
};

}  // namespace cheddar
