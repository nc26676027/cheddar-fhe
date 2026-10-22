#include "extension/BootContext.h"
#include "DiscreteIdentityInternal.h"
#include "extension/Func15RBootExecution.h"

#include <type_traits>

#include "common/Assert.h"
#include "BootstrapTiming.h"

namespace cheddar {
namespace {
template <typename word>
void ValidateBootstrapParameter(const Parameter<word> &param,
                               const BootParameter &boot_param) {
  if (boot_param.route == BootRoute::kFunc15RBoot) {
    AssertTrue(param.log_degree_ == 15 &&
                   param.base_scale_ == boot_param.application_scale &&
                   param.default_encryption_level_ == boot_param.OutputLevel() &&
                   param.max_key_switch_level_ == static_cast<int>(boot_param.q.size()) - 1 &&
                   param.GetDenseHammingWeight() == 1024 &&
                   param.GetSparseHammingWeight() == boot_param.sparse_h,
               "Func15 bootstrap core parameter contract mismatch");
    const std::vector<word> entry{786433};
    const std::vector<word> terminal =
        boot_param.extra_q ? std::vector<word>{word(boot_param.extra_q)} :
                             std::vector<word>{};
    AssertTrue(param.main_primes_ == boot_param.q &&
                   param.aux_primes_ == boot_param.p &&
                   param.ter_primes_ == terminal && param.entry_primes_ == entry,
               "Func15 modulus contract mismatch");
    std::vector<std::pair<int, int>> levels;
    for (size_t i = 1; i <= boot_param.q.size(); ++i) levels.emplace_back(static_cast<int>(i), 0);
    if (boot_param.extra_q) levels.emplace_back(static_cast<int>(boot_param.q.size()), 1);
    AssertTrue(param.level_config_ == levels &&
                   param.additional_base_ == std::make_pair(0, 0),
               "Func15 level contract mismatch");
    return;
  }
  AssertTrue(param.log_degree_ == 16 &&
                 param.base_scale_ == BootParameter::kApplicationScale &&
                 param.default_encryption_level_ == boot_param.OutputLevel() &&
                 param.max_key_switch_level_ == 31 &&
                 param.GetDenseHammingWeight() == 2048 &&
                 param.GetSparseHammingWeight() == boot_param.sparse_h,
             "Bootstrap core parameter contract mismatch");
  const std::vector<word> terminal =
      boot_param.extra_q ? std::vector<word>{word(boot_param.extra_q)} :
                           std::vector<word>{};
  const std::vector<word> entry =
      IsMEBootRoute(boot_param.route) ? std::vector<word>{786433} :
                                      std::vector<word>{};
  AssertTrue(param.main_primes_ == boot_param.q &&
                 param.aux_primes_ == boot_param.p &&
                 param.ter_primes_ == terminal && param.entry_primes_ == entry,
             "Bootstrap modulus contract mismatch");
  std::vector<std::pair<int, int>> levels;
  for (int i = 1; i <= 32; ++i) levels.emplace_back(i, 0);
  if (boot_param.extra_q) levels.emplace_back(32, 1);
  AssertTrue(param.level_config_ == levels &&
                 param.additional_base_ == std::make_pair(0, 0),
             "Bootstrap level contract mismatch");
}
}  // namespace

template <typename word>
BootContext<word>::BootContext(const Parameter<word> &param,
                               const BootParameter &boot_param)
    : Context<word>(param), boot_param_(boot_param) {
  static_assert(std::is_same_v<word, uint64_t>,
                "Identity19 and Ordinary require uint64_t");
  this->encoder_.EnableNearestRounding();
  this->EnableCenteredMergedRescale();
}

template <typename word>
BootContext<word>::BootContext(
    const Parameter<word> &param, const BootParameter &boot_param,
    const DiscreteIdentityParameters &discrete_parameters)
    : Context<word>(param),
      boot_param_(boot_param),
      discrete_parameters_(discrete_parameters) {
  static_assert(std::is_same_v<word, uint64_t>,
                "Discrete Identity requires uint64_t");
  this->encoder_.EnableNearestRounding();
  this->EnableCenteredMergedRescale();
}

template <typename word>
std::shared_ptr<BootContext<word>> BootContext<word>::Create(
    const Parameter<word> &param, const BootParameter &boot_param) {
  ValidateBootstrapParameter(param, boot_param);
  if (IsDiscreteMEBootRoute(boot_param.route)) {
    return Create(param, boot_param, DiscreteIdentityParameters::Certified());
  }
  return std::shared_ptr<BootContext>(new BootContext(param, boot_param));
}

template <typename word>
std::shared_ptr<BootContext<word>> BootContext<word>::Create(
    const Parameter<word> &param, const BootParameter &boot_param,
    const DiscreteIdentityParameters &discrete_parameters) {
  AssertTrue(IsDiscreteMEBootRoute(boot_param.route),
             "Discrete parameters require the discrete MEBOOT route");
  discrete_parameters.Validate();
  ValidateBootstrapParameter(param, boot_param);
  return std::shared_ptr<BootContext>(
      new BootContext(param, boot_param, discrete_parameters));
}

template <typename word>
void BootContext<word>::Prepare() {
  if (route_execution_) return;
  if (IsDiscreteMEBootRoute(boot_param_.route)) {
    AssertTrue(discrete_parameters_.has_value(),
               "Discrete MEBOOT parameters were not supplied");
    route_execution_ = std::make_unique<MEBOOTDiscreteIdentity>(
        this->shared_from_this(), boot_param_, *discrete_parameters_);
    return;
  }
  if (boot_param_.route == BootRoute::kFunc15RBoot) {
    route_execution_ = std::make_unique<Func15RBootExecution>(
        this->shared_from_this(), boot_param_);
    return;
  }
  auto execution = std::make_unique<MEBootExecution>(
      this->shared_from_this(), boot_param_);
  execution->VerifyEncodedLCRBounds();
  route_execution_ = std::move(execution);
}

template <typename word>
bool BootContext<word>::IsBootPrepared(int num_slots) const {
  return route_execution_ && num_slots == boot_param_.slots;
}

template <typename word>
const BootstrapExecution &BootContext<word>::GetRouteExecution() const {
  AssertTrue(route_execution_ != nullptr,
             "Call BootContext::Prepare before evaluation");
  return *route_execution_;
}

template <typename word>
const MEBootExecution &BootContext<word>::GetExecution() const {
  AssertTrue(boot_param_.route == BootRoute::kIdentity19,
             "GetExecution is only available for native MEBOOT");
  return dynamic_cast<const MEBootExecution &>(GetRouteExecution());
}

template <typename word>
const MEBootCtS<word> &BootContext<word>::GetMEBootCtS(int num_slots) const {
  AssertTrue(IsBootPrepared(num_slots), "MEBOOT requires prepared full slots");
  return GetRouteExecution().GetMEBootCtS();
}

template <typename word>
void BootContext<word>::AddRequiredRotations(EvkRequest &req,
                                            int num_slots) const {
  AssertTrue(IsBootPrepared(num_slots), "Bootstrap requires prepared full slots");
  GetRouteExecution().AddRequiredRotations(req);
}

template <typename word>
void BootContext<word>::Boot(Ct &res, const Ct &input,
                             const EvkMap<word> &keys) const {
  GetRouteExecution().Evaluate(res, input, keys);
}

template <typename word>
BootProfile BootContext<word>::ProfileBoot(
    Ct &res, const Ct &input, const EvkMap<word> &keys) const {
  return detail::BootstrapTiming::Measure<BootProfile>(
      [&](const detail::BootstrapTiming::Marker &mark) {
        GetRouteExecution().Evaluate(res, input, keys, mark);
      });
}

template class BootContext<uint64_t>;

}  // namespace cheddar
