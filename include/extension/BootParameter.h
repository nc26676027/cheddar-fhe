#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/Parameter.h"

namespace cheddar {

enum class BootRoute { kMEBoot, kOrdinary };

// Frozen Identity19 MEBOOT and Ordinary bootstrap contracts.
struct BootParameter {
  BootRoute route;
  std::vector<uint64_t> q, p;
  std::vector<double> coefficients;
  std::vector<int> widths, bases;
  uint64_t extra_q;
  int sparse_h, extension, basis_lift, mod1_level, k, double_angle;
  double q_diff, sqrt2pi, interval_a, interval_b;
  std::optional<double> stc_intermediate_log_scale, stc_output_log_scale;

  static constexpr const char *kReferenceRevision =
      "6330f64b178b6215edd35baf5b11c8da558ca95c";
  static constexpr int kSlots = 32768;
  static constexpr int kInputLevel = 2;
  static constexpr double kApplicationScale = 1099511627776.0;
  static constexpr double kMod1Scale = 288230376151711744.0;
  static constexpr double kModUpScale = 281474976710656.0;
  static constexpr double kOutputGain = 0.25;

  static BootParameter Selected(BootRoute route = BootRoute::kMEBoot);
  int OutputLevel() const;
  void ValidateScalingBinding() const;
  std::unique_ptr<Parameter<uint64_t>> MakeParameter() const;
  double Mod1PolynomialTargetScale() const;
  double CtSGain() const { return 1.0 / (k * q_diff); }
  double ScaleDownMultiplier() const;
  double ModUpMultiplier() const;
  double Mod1ReturnGain() const;
  double ProperCtSGain() const { return CtSGain() / Mod1ReturnGain(); }
  double CorrectionGain() const {
    return k * q_diff * kOutputGain * Mod1ReturnGain();
  }
};

}  // namespace cheddar
