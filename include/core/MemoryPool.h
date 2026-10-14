#pragma once

#include <rmm/mr/device/cuda_async_memory_resource.hpp>
#include <rmm/mr/device/per_device_resource.hpp>
#include <rmm/mr/device/statistics_resource_adaptor.hpp>
#include <memory>

#include "core/Parameter.h"

namespace cheddar {

class MemoryPool {
  using DefaultUpstream = rmm::mr::cuda_async_memory_resource;

 public:
  template <typename word>
  explicit MemoryPool(const Parameter<word> &param);
  ~MemoryPool();
  void Trim() const;
  struct Statistics {
    int64_t live_bytes, peak_bytes, allocation_calls;
  };
  // Diagnostic preparation only. Leave disabled for acceptance timings.
  void EnableStatistics();
  Statistics GetStatistics() const;

 private:
  DefaultUpstream base_;
  std::unique_ptr<rmm::mr::statistics_resource_adaptor<DefaultUpstream>> statistics_;
};

}  // namespace cheddar
