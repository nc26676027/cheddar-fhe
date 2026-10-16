#pragma once

#include <functional>

#include "core/Container.h"
#include "core/EvkMap.h"
#include "core/EvkRequest.h"

namespace cheddar {

template <typename word>
class MEBootCtS;

// The BootContext seam hides route-specific orchestration and key requirements.
class BootstrapExecution {
 public:
  using Ct = Ciphertext<uint64_t>;
  using StageMarker = std::function<void(const char *)>;
  using Observer = std::function<void(const char *, const Ct &)>;

  virtual ~BootstrapExecution() = default;
  virtual void AddRequiredRotations(EvkRequest &request) const = 0;
  virtual const MEBootCtS<uint64_t> &GetMEBootCtS() const = 0;
  virtual void Evaluate(Ct &result, const Ct &input,
                        const EvkMap<uint64_t> &keys,
                        const StageMarker &mark = {},
                        const Observer &observe = {}) const = 0;
  virtual int OutputLevel() const = 0;
  virtual double OutputScale() const = 0;
};

}  // namespace cheddar
