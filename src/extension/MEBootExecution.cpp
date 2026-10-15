#include "extension/MEBootExecution.h"

#include <cmath>
#include <limits>
#include "common/Assert.h"
#include "common/PrimeUtils.h"

namespace cheddar {
MEBootExecution::MEBootExecution(ConstContextPtr<uint64_t> context,
                               const BootParameter &profile)
    : context_(context), profile_(profile),
      transforms_(context, profile_), mod1_(context, profile_) {
  profile_.ValidateScalingBinding();
  const double multiplier = profile_.ModUpMultiplier();
  context->encoder_.EncodeConstant(modup_multiplier_, profile_.extra_q ? 32 : 31,
                                   1.0, multiplier);
  // At the final 2^40 units, correction is K*QDiff*0.25 times the
  // pre-Mod1 residues, with their reference 2^40 relabel accounted for.
  const int correction_level = profile_.OutputLevel() + 1;
  context->encoder_.EncodeConstant(correction_, correction_level,
                                   double(profile_.q[correction_level]),
                                   profile_.CorrectionGain());
  if (profile_.route == BootRoute::kMEBoot) {
    meboot_cts_ = std::make_unique<MEBootCtS<uint64_t>>(
        context, BootParameter::kSlots, 32, profile_.ProperCtSGain() / 65536,
        profile_.widths, profile_.bases, profile_.extension);
    // Cauchy-Schwarz bounds the l1 norm of encoded coefficients by sqrt(N)
    // times the maximum diagonal magnitude. Native BigInt truncation
    // contributes at most N; retain a floating encoding guard separately.
    // Entry/centered lift bounds both components by Q0/2 irrespective of noise.
    auto norm_bound = [&](const StripedMatrix &matrix, double scale) {
      long double norm = 0;
      for (const auto &[rotation, diagonal] : matrix) {
        double largest = 0;
        for (auto value : diagonal) largest = std::max(largest, std::abs(value));
        norm += 256.L * largest * scale * (1 + 1024.L * std::numeric_limits<double>::epsilon()) + 65536.L;
      }
      return norm;
    };
    const auto &plans = meboot_cts_->GetPlans();
    long double input_bound = double(profile_.q[0]) / 2 * multiplier;
    long double m1_bound = input_bound * norm_bound(plans[0].matrix, double(profile_.extra_q)) /
                          profile_.extra_q + 1.L;
    const long double ql = profile_.q.back();
    long double projection_bound = m1_bound * norm_bound(plans[1].matrix, double(ql)) / ql + 1.L;
    const long double half_crt = static_cast<long double>(profile_.q[0]) * profile_.q[1] / 2;
    AssertTrue(m1_bound < half_crt && projection_bound < half_crt,
               "Selected joint exceeds two-limb centered CRT; qualification blocked");
    lcr_input_bound_ = double(m1_bound);
    lcr_projection_bound_ = double(projection_bound);
  }
}

std::pair<double, double> VerifyMEBootLCRBounds(
    ConstContextPtr<uint64_t> context, const MEBootCtS<uint64_t> &cts,
    uint64_t lift_multiplier) {
  AssertTrue(context && context->param_.degree_ == 65536 &&
                 context->param_.main_primes_.size() >= 2,
             "Encoded MEBOOT LCR audit requires the full-slot logN16 profile");
  using Wide = unsigned __int128;
  const uint64_t q0 = context->param_.main_primes_[0];
  const uint64_t q1 = context->param_.main_primes_[1];
  const Wide modulus = Wide(q0) * q1;
  const uint64_t inverse = primeutil::InvMod<uint64_t>(q0 % q1, q1);
  auto encoded_l1 = [&](const std::map<int, Plaintext<uint64_t>> &diagonals) {
    Wide sum = 0;
    for (const auto &[rotation, diagonal] : diagonals) {
      DeviceVector<uint64_t> coefficients(diagonal.mx_.size());
      auto view = coefficients.View();
      context->ntt_handler_.INTT(view, diagonal.GetNP(), diagonal.ConstView(), true);
      const int offset = diagonal.GetNP().num_ter_ * 65536;
      HostVector<uint64_t> limbs(2 * 65536);
      AssertTrue(cudaMemcpy(limbs.data(), coefficients.data() + offset,
                            limbs.size() * sizeof(uint64_t), cudaMemcpyDeviceToHost) == cudaSuccess,
                 "Selected encoded norm download failed");
      Wide diagonal_sum = 0;
      for (int i = 0; i < 65536; ++i) {
        const uint64_t r0 = limbs[i], r1 = limbs[65536 + i];
        const uint64_t delta = (r1 + q1 - r0 % q1) % q1;
        const uint64_t digit = uint64_t(Wide(delta) * inverse % q1);
        const Wide value = r0 + Wide(q0) * digit;
        diagonal_sum += value > modulus / 2 ? modulus - value : value;
      }
      sum += diagonal_sum;
    }
    return sum;
  };
  const Wide norm1 = encoded_l1(cts.GetPrefixDiagonals());
  const Wide norm2 = encoded_l1(cts.GetLCRDiagonals());
  // Bound the actual centered entry components after the integer scale lift.
  const Wide lifted_bound = Wide(q0 / 2) * lift_multiplier;
  const auto prefix = context->param_.LevelToNP(cts.GetStartLevel());
  AssertTrue(prefix.num_ter_ == 1 && context->param_.ter_primes_.size() == 1,
             "MEBoot LCR bounds require one exact temporary prefix prime");
  const uint64_t extra_q = context->param_.ter_primes_.front();
  const Wide m1 = (lifted_bound * norm1 + extra_q - 1) / extra_q + 1;
  const uint64_t divisor = context->param_.main_primes_.back();
  const Wide m2 = (m1 * norm2 + divisor - 1) / divisor + 1;
  AssertTrue(m1 < modulus / 2 && m2 < modulus / 2,
             "Selected encoded diagonals exceed two-limb CRT bounds");
  return {double(m1), double(m2)};
}

void MEBootExecution::VerifyEncodedLCRBounds() {
  if (!meboot_cts_) return;
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Selected identity context expired");
  const auto [m1, m2] = VerifyMEBootLCRBounds(
      context, *meboot_cts_, uint64_t(profile_.ModUpMultiplier()));
  AssertTrue(double(m1) <= lcr_input_bound_ && double(m2) <= lcr_projection_bound_,
             "Selected encoded norms exceed the independent conservative envelope");
  encoded_lcr_bound_verified_ = true;
}

void MEBootExecution::Lift(Ciphertext<uint64_t> &res,
                              const Ciphertext<uint64_t> &input, int level) const {
  auto context = context_.lock();
  AssertTrue(context && input.GetNP() == context->param_.LevelToNP(0),
             "Selected centered lift requires Q0");
  const auto np = context->param_.LevelToNP(level);
  Ciphertext<uint64_t> output(np);
  output.SetScale(input.GetScale());
  output.SetNumSlots(input.GetNumSlots());
  for (int component = 0; component < 2; ++component) {
    DeviceVector<uint64_t> coefficient(context->param_.degree_);
    auto cv = coefficient.View();
    auto source = component ? input.AxConstView() : input.BxConstView();
    context->ntt_handler_.INTT(cv, input.GetNP(), source, true);
    auto dst = component ? output.AxView() : output.BxView();
    context->elem_handler_.ModUpToMax(dst, coefficient.ConstView(), np);
    auto view = component ? output.AxConstView() : output.BxConstView();
    context->ntt_handler_.NTT(dst, np, view, true);
  }
  context->MultUnsafe(res, output, modup_multiplier_, level);
  // MultUnsafe keeps the source scale because the multiplier constant is
  // encoded at scale one; the value gain is the integer lift itself.
  context->AssertSameScale(
      res, std::exp2(profile_.stc_output_log_scale.value_or(40)));
  // Relabel to the Mod1 input scale. ValidateScalingBinding proves
  // entry_scale * ModUpMultiplier == kModUpScale for this route.
  res.SetScale(BootParameter::kModUpScale);
}

void MEBootExecution::AddRequiredRotations(EvkRequest &req) const {
  transforms_.AddRequiredRotations(req);
  if (meboot_cts_) meboot_cts_->AddRequiredRotations(req);
}

const MEBootCtS<uint64_t> &MEBootExecution::GetMEBootCtS() const {
  AssertTrue(meboot_cts_ != nullptr, "Ordinary selected route has no joint CtS");
  return *meboot_cts_;
}

void MEBootExecution::Evaluate(Ciphertext<uint64_t> &res,
                                  const Ciphertext<uint64_t> &input,
                                  const EvkMap<uint64_t> &keys,
                                  const std::function<void(const char *)> &mark,
                                  const std::function<void(const char *, const Ciphertext<uint64_t> &)> &observe) const {
  auto context = context_.lock();
  AssertTrue(context != nullptr, "Selected identity context expired");
  AssertTrue(!meboot_cts_ || encoded_lcr_bound_verified_,
             "Verify selected encoded LCR bounds before joint execution");
  Ciphertext<uint64_t> entry, proper, dense;
  transforms_.StC(entry, input, keys);
  if (observe) observe("stc", entry);
  if (mark) mark("StC");
  transforms_.ScaleDown(entry, entry);
  if (observe) observe("scale-down", entry);
  if (mark) mark("ScaleDown");
  if (meboot_cts_) {
    AssertTrue(keys.GetDenseToSparseKey().GetBeta() == 11,
               "Selected joint requires eleven base-16 entry digits");
    context->entry_gadget_->Switch(entry, entry, keys.GetDenseToSparseKey());
  } else {
    context->MultKey(entry, entry, keys.GetDenseToSparseKey());
  }
  if (mark) mark("Entry");
  if (observe) observe("entry", entry);
  Lift(proper, entry, meboot_cts_ ? 32 : 31);
  context->DropLevel(dense, proper, 31);
  context->MultKey(dense, dense, keys.GetSparseToDenseKey());
  if (observe) { observe("proper", proper); observe("dense", dense); }
  if (mark) mark("ModUpAndBypass");
  Ciphertext<uint64_t> main, real, imaginary, conjugate;
  if (meboot_cts_ && observe) {
    Ciphertext<uint64_t> m1, m2, m3;
    meboot_cts_->EvaluateStage1(m1, proper);
    observe("m1", m1);
    meboot_cts_->EvaluateStage2(m2, m1, keys);
    observe("m2", m2);
    meboot_cts_->EvaluateStage3(m3, m2, keys);
    observe("m3", m3);
    meboot_cts_->EvaluateStage4(main, m3, keys);
    observe("m4", main);
  } else if (meboot_cts_) {
    meboot_cts_->Evaluate(main, proper, keys);
  } else {
    transforms_.CtS(main, dense, keys);
  }
  if (mark) mark("CtS");
  if (observe) observe("cts", main);
  context->SplitRealImag(real, imaginary, main,
                         keys.GetConjugationKey());
  if (mark) mark("Split");
  auto correct = [&](Ciphertext<uint64_t> &channel) {
    Ciphertext<uint64_t> correction, nonlinear;
    if (observe) observe("pre-mod1", channel);
    mod1_.Evaluate(nonlinear, channel, keys.GetMultiplicationKey());
    if (observe) observe("mod1", nonlinear);
    const int correction_level = profile_.OutputLevel() + 1;
    context->DropLevel(correction, channel, correction_level);
    correction.SetScale(BootParameter::kApplicationScale);
    context->MultUnsafe(correction, correction, correction_, correction_level);
    context->Rescale(correction, correction);
    context->AssertSameScale(correction, BootParameter::kApplicationScale);
    if (observe) observe("correction", correction);
    // Mod1 output is restored to application units as in the pinned finish.
    context->AssertSameScale(nonlinear, BootParameter::kModUpScale);
    nonlinear.SetScale(BootParameter::kApplicationScale);
    context->Sub(channel, nonlinear, correction);
    if (observe) observe("corrected", channel);
  };
  correct(real);
  if (mark) mark("Mod1AndCorrectionReal");
  correct(imaginary);
  if (mark) mark("Mod1AndCorrectionImag");
  context->MultImaginaryUnit(imaginary, imaginary);
  context->Add(main, real, imaginary);
  transforms_.Bypass(dense, dense, keys);
  if (observe) observe("bypass-cts", dense);
  context->SplitRealImag(real, imaginary, dense,
                         keys.GetConjugationKey());
  context->MultImaginaryUnit(imaginary, imaginary);
  context->Add(dense, real, imaginary);
  context->AssertSameScale(main, BootParameter::kApplicationScale);
  dense.SetScale(BootParameter::kApplicationScale);
  context->Add(res, main, dense);
  if (observe) observe("output", res);
  if (mark) mark("BypassAndFinalCombine");
}
}  // namespace cheddar
