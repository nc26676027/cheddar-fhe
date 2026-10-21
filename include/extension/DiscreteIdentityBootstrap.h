#pragma once

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace cheddar {

struct DiscreteIdentityParameters {
  int width = 0;
  double input_scale = 0;
  double extraction_scale = 0;
  double max_input_error = 0;
  double max_digit_error = 0;
  std::string name;
  std::vector<std::vector<double>> extractor_coefficients;
  // MEBOOT affine input stage and cubic rescale schedule; ordinary is unchanged.
  int cleaner_alignment_level = 29;
  std::vector<int> cleaning_rescales{1, 1};
  double argument_gain = 0;
  int extraction_baby_step = 0;

  static DiscreteIdentityParameters FromCoefficients(
      int width, std::vector<std::vector<double>> coefficients,
      const std::string &name);
  static DiscreteIdentityParameters FromHW40RemezCoefficients(
      std::vector<std::vector<double>> coefficients,
      int argument_divisor, int affine_scale, int affine_integer);
  static DiscreteIdentityParameters Certified();
  void Validate() const;
};

struct DiscreteIdentityStageTiming {
  std::string name;
  float gpu_ms = 0;
  double wall_ms = 0;
};

struct DiscreteIdentityTiming {
  std::vector<DiscreteIdentityStageTiming> stages;
  float gpu_ms = 0;
  double wall_ms = 0;
};

}  // namespace cheddar
