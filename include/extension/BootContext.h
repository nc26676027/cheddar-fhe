#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Context.h"
#include "extension/BootParameter.h"
#include "extension/BootstrapExecution.h"
#include "extension/DiscreteIdentityBootstrap.h"
#include "extension/MEBootExecution.h"

namespace cheddar {

struct BootProfileStage {
  std::string name;
  float gpu_ms;
  double host_submission_ms;
};

struct BootProfile {
  std::vector<BootProfileStage> stages;
  float gpu_ms;
  double wall_ms;
};

// Full-slot uint64 bootstrapping through Cheddar's native GPU operators.
template <typename word>
class BootContext : public Context<word>,
                    public std::enable_shared_from_this<BootContext<word>> {
 public:
  using Ct = Ciphertext<word>;
  static std::shared_ptr<BootContext> Create(
      const Parameter<word> &param, const BootParameter &boot_param);
  static std::shared_ptr<BootContext> Create(
      const Parameter<word> &param, const BootParameter &boot_param,
      const DiscreteIdentityParameters &discrete_parameters);
  void Prepare();
  bool IsBootPrepared(int num_slots) const;
  void AddRequiredRotations(EvkRequest &req, int num_slots) const;
  void Boot(Ct &res, const Ct &input, const EvkMap<word> &keys) const;
  BootProfile ProfileBoot(Ct &res, const Ct &input,
                          const EvkMap<word> &keys) const;
  const MEBootCtS<word> &GetMEBootCtS(int num_slots) const;

  // Diagnostics observe the same execution used by Boot().
  // Native MEBOOT diagnostics retained for existing callers.
  const MEBootExecution &GetExecution() const;
  const BootstrapExecution &GetRouteExecution() const;
  const BootParameter boot_param_;
  BootContext(const BootContext &) = delete;
  BootContext &operator=(const BootContext &) = delete;
  virtual ~BootContext() = default;

 private:
  BootContext(const Parameter<word> &param, const BootParameter &boot_param);
  BootContext(const Parameter<word> &param, const BootParameter &boot_param,
              const DiscreteIdentityParameters &discrete_parameters);
  std::unique_ptr<BootstrapExecution> route_execution_;
  std::optional<DiscreteIdentityParameters> discrete_parameters_;
};

}  // namespace cheddar
