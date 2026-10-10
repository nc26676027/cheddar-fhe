#include "core/MemoryPool.h"

namespace cheddar {

template <typename word>
MemoryPool::MemoryPool(const Parameter<word> &param)
    : base_(static_cast<std::size_t>(0), static_cast<std::size_t>(0)),
      memory_pool_(&base_) {
  (void)param;
  rmm::mr::set_current_device_resource(&base_);
}

MemoryPool::~MemoryPool() {
  cudaDeviceSynchronize();
  // reset to cuda_device_resource
  rmm::mr::set_current_device_resource(nullptr);
}

void MemoryPool::Trim() const {
  cudaDeviceSynchronize();
  cudaMemPoolTrimTo(base_.pool_handle(), 0);
}

template MemoryPool::MemoryPool(const Parameter<uint32_t> &param);
template MemoryPool::MemoryPool(const Parameter<uint64_t> &param);

}  // namespace cheddar