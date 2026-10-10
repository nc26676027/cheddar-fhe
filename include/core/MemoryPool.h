#pragma once

#include <rmm/mr/device/cuda_async_memory_resource.hpp>
#include <rmm/mr/device/per_device_resource.hpp>

#include "core/Parameter.h"

namespace cheddar {

class MemoryPool {
  using DefaultUpstream = rmm::mr::cuda_async_memory_resource;

 public:
  template <typename word>
  explicit MemoryPool(const Parameter<word> &param);
  ~MemoryPool();
  void Trim() const;

 private:
  DefaultUpstream base_;
};

}  // namespace cheddar