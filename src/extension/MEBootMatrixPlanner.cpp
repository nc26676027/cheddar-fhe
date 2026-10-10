#include "extension/MEBootMatrixPlanner.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "common/Assert.h"
#include "common/CommonUtils.h"

namespace cheddar {

std::vector<std::vector<int>> MEBootMatrixPlanner::ComputeSupports(
    int log_slots, const std::vector<int> &widths) {
  AssertTrue(log_slots >= 1 && log_slots <= 20, "Invalid log_slots domain");
  AssertTrue(!widths.empty(), "Widths must not be empty");

  int sum = 0;
  for (int w : widths) {
    AssertTrue(w >= 1 && w <= log_slots, "Invalid stage width");
    sum += w;
  }
  AssertTrue(sum == log_slots, "Sum of stage widths must equal log_slots");

  int slots = 1 << log_slots;
  int remaining = log_slots;
  std::vector<std::vector<int>> out(widths.size());

  for (size_t i = 0; i < widths.size(); i++) {
    int w = widths[i];
    remaining -= w;
    int stride = 1 << remaining;
    std::set<int> seen;

    int max_shift = (1 << w) - 1;
    for (int shift = -max_shift; shift <= max_shift; shift++) {
      int offset = (shift * stride) & (slots - 1);
      seen.insert(offset);
    }

    out[i].assign(seen.begin(), seen.end());
    std::sort(out[i].begin(), out[i].end());
  }

  return out;
}

template <typename word>
std::vector<StripedMatrix>
MEBootMatrixPlanner::GenerateElementaryCtSStages(
    ConstContextPtr<word> context, int num_slots) {
  int M = context->param_.degree_ * 2;
  const auto &encoder = context->encoder_;
  int num_stages = Log2Ceil(num_slots);

  std::vector<StripedMatrix> cts_stages(num_stages);

  for (int i = 0; i < num_stages; i++) {
    int stride = 1 << i;
    int stride_group_size = stride * 2;
    int st8 = stride << 3;
    int gap = M / st8;

    // Multiplication order for CtS: high strides to low strides
    auto &cts_target = cts_stages[num_stages - i - 1];
    cts_target = StripedMatrix(num_slots, num_slots);

    cts_target.try_emplace(0, num_slots, Complex(0));
    cts_target.try_emplace(stride, num_slots, Complex(0));
    if (i != num_stages - 1) {
      cts_target.try_emplace(num_slots - stride, num_slots, Complex(0));
    }

    auto &cts_diag_0 = cts_target[0];
    auto &cts_diag_plus = cts_target[stride];
    auto &cts_diag_minus = cts_target[num_slots - stride];

    for (int j = 0; j < stride; j++) {
      int cts_twiddle_index =
          (st8 - (context->param_.GetGaloisFactor(j) % st8)) * gap;
      Complex cts_twiddle = encoder.GetTwiddleFactor(cts_twiddle_index);

      // CtS butterfly: (x, y) = (x + y, (x - y) * twiddle)
      cts_diag_0[j] = 1;
      cts_diag_plus[j] = 1;
      cts_diag_minus[j + stride] = cts_twiddle;
      cts_diag_0[j + stride] = -cts_twiddle;
    }

    int num_double = Log2Ceil(num_slots / stride_group_size);
    for (int r = 0; r < num_double; r++) {
      std::copy(cts_diag_0.begin(),
                cts_diag_0.begin() + stride_group_size * (1 << r),
                cts_diag_0.begin() + stride_group_size * (1 << r));
      std::copy(cts_diag_plus.begin(),
                cts_diag_plus.begin() + stride_group_size * (1 << r),
                cts_diag_plus.begin() + stride_group_size * (1 << r));
      if (i != num_stages - 1) {
        std::copy(cts_diag_minus.begin(),
                  cts_diag_minus.begin() + stride_group_size * (1 << r),
                  cts_diag_minus.begin() + stride_group_size * (1 << r));
      }
    }
  }

  return cts_stages;
}

template <typename word>
std::vector<MEBootStagePlan> MEBootMatrixPlanner::PlanCtS(
    ConstContextPtr<word> context, int num_slots,
    const std::vector<int> &widths /*= {4, 4, 3, 4}*/,
    const std::vector<int> &dense_bases /*= {64, 8}*/,
    double scaling_factor /*= 1.0*/) {
  int log_slots = Log2Ceil(num_slots);
  AssertTrue((1 << log_slots) == num_slots, "num_slots must be a power of 2");

  auto supports = ComputeSupports(log_slots, widths);
  auto elementary = GenerateElementaryCtSStages(context, num_slots);

  int num_phases = widths.size();
  std::vector<MEBootStagePlan> plans(num_phases);

  double phase_scale = std::pow(scaling_factor, 1.0 / num_phases);

  int cumulative_stages = 0;
  int remaining_radix = log_slots;

  for (int i = 0; i < num_phases; i++) {
    int w = widths[i];
    remaining_radix -= w;

    StripedMatrix phase_matrix = elementary[cumulative_stages];
    for (int j = cumulative_stages + 1; j < cumulative_stages + w; j++) {
      phase_matrix = StripedMatrix::Mult(elementary[j], phase_matrix);
    }

    if (scaling_factor != 1.0) {
      double stage_scale = phase_scale;
      if constexpr (sizeof(word) == 4) {
        if (num_phases == 4) {
          constexpr double kStage2Boost = 64.0;
          if (i == 0) {
            stage_scale = std::pow(scaling_factor, 0.5);
          } else if (i == 1) {
            stage_scale = kStage2Boost;
          } else {
            stage_scale =
                std::pow(scaling_factor, 0.25) / std::sqrt(kStage2Boost);
          }
        }
      }
      phase_matrix = StripedMatrix::Mult(phase_matrix, stage_scale);
    }

    int bs = 1;
    int gs = 1;
    if (i >= 2 && static_cast<size_t>(i - 2) < dense_bases.size()) {
      int base = dense_bases[i - 2];
      bs = base;
      gs = DivCeil(phase_matrix.GetNumDiag(), bs);
      if (gs < 1) gs = 1;
    } else {
      int num_diag = phase_matrix.GetNumDiag();
      bs = 1 << DivCeil(Log2Ceil(num_diag), 2);
      gs = DivCeil(num_diag, bs);
    }

    plans[i] = MEBootStagePlan{
        i,
        w,
        1 << remaining_radix,
        supports[i],
        phase_matrix,
        bs,
        gs,
    };

    cumulative_stages += w;
  }

  return plans;
}

template std::vector<StripedMatrix>
MEBootMatrixPlanner::GenerateElementaryCtSStages<uint32_t>(
    ConstContextPtr<uint32_t>, int);
template std::vector<StripedMatrix>
MEBootMatrixPlanner::GenerateElementaryCtSStages<uint64_t>(
    ConstContextPtr<uint64_t>, int);

template std::vector<MEBootStagePlan>
MEBootMatrixPlanner::PlanCtS<uint32_t>(
    ConstContextPtr<uint32_t>, int, const std::vector<int> &,
    const std::vector<int> &, double);
template std::vector<MEBootStagePlan>
MEBootMatrixPlanner::PlanCtS<uint64_t>(
    ConstContextPtr<uint64_t>, int, const std::vector<int> &,
    const std::vector<int> &, double);

}  // namespace cheddar
