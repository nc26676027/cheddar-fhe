#include "extension/Func15RBootExecution.h"

#include <cmath>
#include "common/Assert.h"
#include "extension/MEBootMatrixPlanner.h"

namespace cheddar {
namespace {

using Transform = LinearTransform<uint64_t>;

StripedMatrix Merge(const std::vector<StripedMatrix> &stages, int begin,
                    int width) {
  auto result = stages.at(begin);
  for (int i = 1; i < width; ++i) {
    result = StripedMatrix::Mult(stages.at(begin + i), result);
  }
  return result;
}

}  // namespace

Func15RBootExecution::Func15RBootExecution(ConstContextPtr<uint64_t> context,
                                           const BootParameter &profile)
    : context_(context), profile_(profile), evalmod_(context, profile) {
  profile.ValidateScalingBinding();
  AssertTrue(profile.route == BootRoute::kFunc15RBoot,
             "Func15RBootExecution requires kFunc15RBoot route");
  AssertTrue(profile.slots == 16384 && profile.log_degree == 15,
             "Func15RBootExecution requires logN=15, 16384 slots");
  AssertTrue(profile.extra_q == 9007199255658497,
             "Func15RBootExecution requires Level 19 terminal prime extra_q");

  // 1. Build StC stages [7, 7]
  const int slots = profile.slots;
  auto stc_elementary =
      MEBootMatrixPlanner::GenerateElementaryStCStages(context, slots);
  // Total gain for StC: factor 8.0 without double-normalizing slots
  const double gain = 8.0;
  const double middle =
      std::exp2(profile.stc_intermediate_log_scale.value_or(37.875));
  stc_output_scale_ =
      std::exp2(profile.stc_output_log_scale.value_or(31.0));

  const std::vector<int> stc_widths{7, 7};
  int cursor = 0;
  for (int i = 0; i < 2; ++i) {
    auto matrix = StripedMatrix::Mult(
        Merge(stc_elementary, cursor, stc_widths[i]), std::sqrt(gain));
    auto [bs, gs] = Transform::DefaultBSGSSplit(matrix.GetNumDiag() + (i == 0));
    const int pre = i == 0 ? -(1 << stc_widths[0]) : (1 << stc_widths[0]);
    const int additional = i == 0 ? (1 << stc_widths[0]) : 0;
    const double input_scale =
        i == 0 ? profile.application_scale : middle;
    const double output_scale = i == 0 ? middle : stc_output_scale_;
    stc_.emplace_back(context, matrix, 2 - i,
                      double(profile.q[2 - i]) * output_scale / input_scale,
                      bs, gs, pre, additional);
    stc_.back().ReleaseCompactBackings();
    cursor += stc_widths[i];
  }

  // 2. ScaleDown constant at Level 0 (multiplier = 1.0)
  context->encoder_.EncodeConstant(scale_down_, 0, 1.0, 1.0);

  // 3. Lift multiplier constant at Level 19 (ModUp to Q19 * extra_q)
  // Scale increases from 2^31 to 2^43, requiring multiplier 4096.0 at scale 1.0
  const double lift_multiplier_val =
      std::exp2(std::log2(profile.application_scale) -
                profile.stc_output_log_scale.value_or(31.0));
  context->encoder_.EncodeConstant(lift_multiplier_, 19, 1.0,
                                   lift_multiplier_val);

  // 4. Subring CtS [3, 3, 4, 4]
  // Overall CtS gain must map coefficients 8u to u * c2s:
  // gain_cts = CtSGain() / 8.0
  // Since unnormalized elementary CtS has gain 2 * slots = 32768:
  // scaling_factor = gain_cts / 32768 = CtSGain() / (8 * 32768)
  const double cts_gain =
      profile.CtSGain() / (8.0 * static_cast<double>(2 * slots));
  meboot_cts_ = std::make_unique<MEBootCtS<uint64_t>>(
      context, slots, 19, cts_gain, profile.widths, profile.bases,
      profile.extension);
}

void Func15RBootExecution::StC(Ciphertext<uint64_t> &res,
                               const Ciphertext<uint64_t> &input,
                               const EvkMap<uint64_t> &keys) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Context expired");
  AssertTrue(input.GetNumSlots() == profile_.slots &&
                 input.GetNP() == context->param_.LevelToNP(2),
             "Func15 StC requires 16384-slot Level 2 input");
  context->AssertSameScale(input, profile_.application_scale);

  stc_[0].Evaluate(context, res, input, keys);
  stc_[1].Evaluate(context, res, res, keys);
  context->AssertSameScale(res, stc_output_scale_);
}

void Func15RBootExecution::ScaleDown(Ciphertext<uint64_t> &res,
                                     const Ciphertext<uint64_t> &input) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr && input.GetNP() == context->param_.LevelToNP(0),
             "Func15 ScaleDown requires Level 0");
  context->AssertSameScale(input, stc_output_scale_);
  context->MultUnsafe(res, input, scale_down_, 0);
}

void Func15RBootExecution::Lift(Ciphertext<uint64_t> &res,
                                const Ciphertext<uint64_t> &input) const {
  auto context = context_.lock();
  AssertTrue(context && input.GetNP() == context->param_.LevelToNP(0),
             "Func15 centered lift requires Q0");
  const auto np = context->param_.LevelToNP(19);
  Ciphertext<uint64_t> output(np);
  output.SetScale(input.GetScale());
  output.SetNumSlots(input.GetNumSlots());
  for (int component = 0; component < 2; ++component) {
    DeviceVector<uint64_t> coefficients(context->param_.degree_);
    auto view = coefficients.View();
    const auto source = component ? input.AxConstView() : input.BxConstView();
    context->ntt_handler_.INTT(view, input.GetNP(), source, true);
    auto destination = component ? output.AxView() : output.BxView();
    context->elem_handler_.ModUpToMax(destination, coefficients.ConstView(), np);
    const auto lifted = component ? output.AxConstView() : output.BxConstView();
    context->ntt_handler_.NTT(destination, np, lifted, true);
  }
  context->MultUnsafe(res, output, lift_multiplier_, 19);
  context->AssertSameScale(res, stc_output_scale_);
  res.SetScale(profile_.application_scale);
}

void Func15RBootExecution::AddRequiredRotations(EvkRequest &req) const {
  for (const auto &factor : stc_) {
    factor.AddRequiredRotations(req);
  }
  if (meboot_cts_) {
    meboot_cts_->AddRequiredRotations(req);
  }
}

const MEBootCtS<uint64_t> &Func15RBootExecution::GetMEBootCtS() const {
  AssertTrue(meboot_cts_ != nullptr, "MEBootCtS not initialized");
  return *meboot_cts_;
}

void Func15RBootExecution::Evaluate(
    Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
    const EvkMap<uint64_t> &keys, const StageMarker &mark,
    const Observer &observe) const {
  Evaluate(res, input, keys, FunctionalMode::kActivation, true, mark, observe);
}

void Func15RBootExecution::Evaluate(
    Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &input,
    const EvkMap<uint64_t> &keys, FunctionalMode mode, bool real_only,
    const StageMarker &mark, const Observer &observe) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Context expired");
  AssertTrue(input.GetNumSlots() == profile_.slots &&
                 input.GetNP() == context->param_.LevelToNP(2),
             "Func15RBoot input must be Level 2 with 16384 slots");
  context->AssertSameScale(input, profile_.application_scale);

  AssertTrue(keys.GetDenseToSparseKey().GetBeta() == 9,
             "Func15RBoot requires 9 base-16 entry digits");

  // 1. StC [7, 7]: Level 2 -> Level 0
  Ciphertext<uint64_t> entry;
  StC(entry, input, keys);
  if (observe) observe("stc", entry);
  if (mark) mark("StC");

  EvaluatePostStC(res, entry, keys, mode, real_only, mark, observe);
}

void Func15RBootExecution::EvaluatePostStC(
    Ciphertext<uint64_t> &res, const Ciphertext<uint64_t> &stc_output,
    const EvkMap<uint64_t> &keys, FunctionalMode mode, bool real_only,
    const StageMarker &mark, const Observer &observe) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Context expired");
  AssertTrue(stc_output.GetNP() == context->param_.LevelToNP(0),
             "Func15RBoot post-StC input must be Level 0");
  AssertTrue(keys.GetDenseToSparseKey().GetBeta() == 9,
             "Func15RBoot requires 9 base-16 entry digits");

  // 2. ScaleDown at Level 0
  Ciphertext<uint64_t> entry;
  ScaleDown(entry, stc_output);
  if (observe) observe("scale-down", entry);
  if (mark) mark("ScaleDown");

  // 3. Entry gadget key switch: dense -> subring (H=80, N'=4096)
  context->entry_gadget_->Switch(entry, entry, keys.GetDenseToSparseKey());
  if (observe) observe("entry", entry);
  if (mark) mark("Entry");

  // 4. Centered Lift to Level 19 (ModUp to Q19 * extra_q)
  Ciphertext<uint64_t> raised;
  Lift(raised, entry);
  if (observe) observe("lift", raised);
  if (mark) mark("Lift");

  // 5. Subring CtS [3, 3, 4, 4]: Level 19 -> Level 16
  Ciphertext<uint64_t> m1, m2, m3, main;
  meboot_cts_->EvaluateStage1(m1, raised);
  if (observe) observe("cts-s1", m1);
  if (mark) mark("CtS-Stage1");

  meboot_cts_->EvaluateStage2(m2, m1, keys);
  if (observe) observe("cts-s2", m2);
  if (mark) mark("CtS-Stage2");

  meboot_cts_->EvaluateStage3(m3, m2, keys);
  if (observe) observe("cts-s3", m3);
  if (mark) mark("CtS-Stage3");

  meboot_cts_->EvaluateStage4(main, m3, keys);
  if (observe) observe("cts-s4", main);
  if (mark) mark("CtS-Stage4");

  // 6. Split real (or real + imag)
  if (real_only) {
    Ciphertext<uint64_t> real, conjugate;
    context->HConj(conjugate, main, keys.GetConjugationKey());
    context->Add(real, main, conjugate);
    real.SetNumSlots(main.GetNumSlots());
    real.SetScale(main.GetScale());
    if (mark) mark("SplitReal");

    // 7. FunctionalEvalMod (Chebyshev + DA2 + Tails): Level 16 -> Level 3
    evalmod_.Evaluate(res, real, keys.GetMultiplicationKey(), mode, observe);
    if (mark) mark("EvalMod");
  } else {
    Ciphertext<uint64_t> real, imag;
    context->SplitRealImag(real, imag, main, keys.GetConjugationKey());
    if (mark) mark("Split");

    // 7. FunctionalEvalMod (Chebyshev + DA2 + Tails): Level 16 -> Level 3
    Ciphertext<uint64_t> real_out, imag_out;
    evalmod_.Evaluate(real_out, real, keys.GetMultiplicationKey(), mode, observe);
    evalmod_.Evaluate(imag_out, imag, keys.GetMultiplicationKey(), mode, observe);
    if (mark) mark("EvalMod");

    // 8. Recombine real and imaginary parts
    context->MultImaginaryUnit(imag_out, imag_out);
    context->Add(res, real_out, imag_out);
  }
  res.SetScale(profile_.application_scale);
  if (observe) observe("output", res);
  if (mark) mark("FinalCombine");
}

}  // namespace cheddar
