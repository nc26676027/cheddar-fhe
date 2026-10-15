#include "extension/BootTransforms.h"

#include <cmath>

#include "common/Assert.h"
#include "extension/MEBootMatrixPlanner.h"

namespace cheddar {
namespace {
using Transform = LinearTransform<uint64_t>;
StripedMatrix Merge(const std::vector<StripedMatrix> &stages, int begin, int width) {
  auto result = stages.at(begin);
  for (int i = 1; i < width; ++i) {
    result = StripedMatrix::Mult(stages.at(begin + i), result);
  }
  return result;
}

void Evaluate(ConstContextPtr<uint64_t> context, Ciphertext<uint64_t> &res,
              const Ciphertext<uint64_t> &input, const EvkMap<uint64_t> &keys,
              const std::vector<Transform> &factors) {
  AssertTrue(!factors.empty(), "Selected dense transform not prepared");
  factors.front().Evaluate(context, res, input, keys);
  for (size_t i = 1; i < factors.size(); ++i) factors[i].Evaluate(context, res, res, keys);
}
}  // namespace

BootTransforms::BootTransforms(ConstContextPtr<uint64_t> context,
                                         const BootParameter &profile)
    : context_(context), profile_(profile) {
  constexpr int slots = BootParameter::kSlots;
  auto stc = MEBootMatrixPlanner::GenerateElementaryStCStages(context, slots);
  auto cts = MEBootMatrixPlanner::GenerateElementaryCtSStages(context, slots);
  const double gain = 4 * BootParameter::kApplicationScale /
                      BootParameter::kModUpScale;
  const double middle = std::exp2(profile.stc_intermediate_log_scale.value_or(40));
  stc_output_scale_ = std::exp2(profile.stc_output_log_scale.value_or(40));
  const std::vector<int> stc_widths{7, 8};
  int cursor = 0;
  for (int i = 0; i < 2; ++i) {
    auto matrix = StripedMatrix::Mult(Merge(stc, cursor, stc_widths[i]), std::sqrt(gain));
    auto [bs, gs] = Transform::DefaultBSGSSplit(matrix.GetNumDiag() + (i == 0));
    const int pre = i == 0 ? -(1 << stc_widths[0]) : (1 << stc_widths[0]);
    const int additional = i == 0 ? (1 << stc_widths[0]) : 0;
    const double input_scale = i == 0 ? BootParameter::kApplicationScale : middle;
    const double output_scale = i == 0 ? middle : stc_output_scale_;
    stc_.emplace_back(context, matrix, 2 - i, double(profile.q[2 - i]) * output_scale / input_scale,
                      bs, gs, pre, additional);
    stc_.back().ReleaseCompactBackings();
    cursor += stc_widths[i];
  }
  // ScaleDown's integer multiplier is rounded exactly once, before entry.
  const double multiplier = profile.ScaleDownMultiplier();
  AssertTrue(multiplier >= 1, "Selected ScaleDown requires a positive integer multiplier");
  context->encoder_.EncodeConstant(scale_down_, 0, 1.0, multiplier);

  if (profile.route == BootRoute::kOrdinary) {
    cursor = 0;
    const double per_factor_gain = std::pow(profile.CtSGain() / (2 * slots), 1.0 / 3);
    for (int i = 0; i < 3; ++i) {
      auto matrix = StripedMatrix::Mult(Merge(cts, cursor, profile.widths[i]), per_factor_gain);
      cts_.emplace_back(context, matrix, 31 - i, double(profile.q[31 - i]), profile.bases[i]);
      cts_.back().ReleaseCompactBackings();
      cursor += profile.widths[i];
    }
  }
  // The reference CtS bypass uses Mod1 depth factors; the early factors consume
  // the rounding budget instead of sharing the low-precision proper matrices.
  const int depth = profile.mod1_level - profile.OutputLevel();
  const double per_factor_gain = std::pow(0.25 / (2 * slots), 1.0 / depth);
  cursor = 0;
  int remaining = 15;
  for (int i = 0; i < depth; ++i) {
    const int width = (remaining + depth - i - 1) / (depth - i);
    remaining -= width;
    auto matrix = StripedMatrix::Mult(Merge(cts, cursor, width), per_factor_gain);
    int remaining_radix = 15 - cursor - width;
    auto [bs, gs] = Transform::DefaultBSGSSplit(matrix.GetNumDiag() + (i == depth - 1));
    const int pre = i == 0 ? (1 << remaining_radix) :
                    i == depth - 1 ? -(1 << width) :
                    -((1 << width) - 1) * (1 << remaining_radix);
    const int additional = i == depth - 1 ? 0 : -(1 << remaining_radix);
    bypass_.emplace_back(context, matrix, profile.mod1_level - i,
                        double(profile.q[profile.mod1_level - i]), bs, gs, pre, additional);
    bypass_.back().ReleaseCompactBackings();
    cursor += width;
  }
}

void BootTransforms::StC(Ciphertext<uint64_t> &res,
                              const Ciphertext<uint64_t> &input,
                              const EvkMap<uint64_t> &keys) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Selected transform context expired");
  AssertTrue(input.GetNumSlots() == BootParameter::kSlots &&
                 input.GetNP() == context->param_.LevelToNP(2),
             "Selected StC requires full-slot level-2 input");
  context->AssertSameScale(input, BootParameter::kApplicationScale);
  Evaluate(context, res, input, keys, stc_);
  context->AssertSameScale(res, stc_output_scale_);
}

void BootTransforms::ScaleDown(Ciphertext<uint64_t> &res,
                                    const Ciphertext<uint64_t> &input) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr && input.GetNP() == context->param_.LevelToNP(0),
             "Selected ScaleDown requires level zero");
  context->AssertSameScale(input, stc_output_scale_);
  context->MultUnsafe(res, input, scale_down_, 0);
}

void BootTransforms::CtS(Ciphertext<uint64_t> &res,
                              const Ciphertext<uint64_t> &input,
                              const EvkMap<uint64_t> &keys) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Selected transform context expired");
  Evaluate(context, res, input, keys, cts_);
}

void BootTransforms::Bypass(Ciphertext<uint64_t> &res,
                                 const Ciphertext<uint64_t> &input,
                                 const EvkMap<uint64_t> &keys) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Selected transform context expired");
  Ciphertext<uint64_t> reduced;
  context->DropLevel(reduced, input, profile_.mod1_level);
  Evaluate(context, res, reduced, keys, bypass_);
}

void BootTransforms::AddRequiredRotations(EvkRequest &req) const {
  for (const auto *group : {&stc_, &cts_, &bypass_}) {
    for (const auto &factor : *group) factor.AddRequiredRotations(req);
  }
}
}  // namespace cheddar
