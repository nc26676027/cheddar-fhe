#include "extension/BootContext.h"

#include <array>
#include <chrono>
#include <type_traits>

#include "common/Assert.h"

namespace cheddar {

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
std::shared_ptr<BootContext<word>> BootContext<word>::Create(
    const Parameter<word> &param, const BootParameter &boot_param) {
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
      boot_param.route == BootRoute::kMEBoot ? std::vector<word>{786433} :
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
  return std::shared_ptr<BootContext>(new BootContext(param, boot_param));
}

template <typename word>
void BootContext<word>::Prepare() {
  if (execution_) return;
  auto execution = std::make_unique<MEBootExecution>(
      this->shared_from_this(), boot_param_);
  execution->VerifyEncodedLCRBounds();
  execution_ = std::move(execution);
}

template <typename word>
bool BootContext<word>::IsBootPrepared(int num_slots) const {
  return execution_ && num_slots == BootParameter::kSlots;
}

template <typename word>
const MEBootExecution &BootContext<word>::GetExecution() const {
  AssertTrue(execution_ != nullptr, "Call BootContext::Prepare before evaluation");
  return *execution_;
}

template <typename word>
const MEBootCtS<word> &BootContext<word>::GetMEBootCtS(int num_slots) const {
  AssertTrue(IsBootPrepared(num_slots), "MEBOOT requires prepared full slots");
  return GetExecution().GetMEBootCtS();
}

template <typename word>
void BootContext<word>::AddRequiredRotations(EvkRequest &req,
                                            int num_slots) const {
  AssertTrue(IsBootPrepared(num_slots), "Bootstrap requires prepared full slots");
  GetExecution().AddRequiredRotations(req);
}

template <typename word>
void BootContext<word>::Boot(Ct &res, const Ct &input,
                             const EvkMap<word> &keys) const {
  GetExecution().Evaluate(res, input, keys);
}

template <typename word>
BootProfile BootContext<word>::ProfileBoot(
    Ct &res, const Ct &input, const EvkMap<word> &keys) const {
  struct Events {
    std::array<cudaEvent_t, 16> values{};
    ~Events() {
      for (auto event : values) if (event) cudaEventDestroy(event);
    }
  } events;
  for (auto &event : events.values) {
    AssertTrue(cudaEventCreate(&event) == cudaSuccess,
               "ProfileBoot: event creation failed");
  }
  BootProfile result;
  result.stages.reserve(events.values.size() - 1);
  size_t cursor = 0;
  AssertTrue(cudaDeviceSynchronize() == cudaSuccess,
             "ProfileBoot: initial synchronization failed");
  const auto begin = std::chrono::steady_clock::now();
  cudaEventRecord(events.values[0]);
  auto previous = begin;
  auto mark = [&](const char *name) {
    AssertTrue(cursor + 1 < events.values.size(), "ProfileBoot: too many stages");
    cudaEventRecord(events.values[++cursor]);
    const auto now = std::chrono::steady_clock::now();
    result.stages.push_back({name, 0,
        std::chrono::duration<double, std::milli>(now - previous).count()});
    previous = now;
  };
  GetExecution().Evaluate(res, input, keys, mark);
  mark("ReleaseTemps");
  AssertTrue(cudaEventSynchronize(events.values[cursor]) == cudaSuccess,
             "ProfileBoot: bootstrap failed");
  result.wall_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - begin).count();
  cudaEventElapsedTime(&result.gpu_ms, events.values[0], events.values[cursor]);
  for (size_t i = 0; i < result.stages.size(); ++i) {
    cudaEventElapsedTime(&result.stages[i].gpu_ms,
                        events.values[i], events.values[i + 1]);
  }
  return result;
}

template class BootContext<uint64_t>;

}  // namespace cheddar
