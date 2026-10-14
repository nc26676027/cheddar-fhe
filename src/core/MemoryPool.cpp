#include "core/MemoryPool.h"

namespace cheddar {

template <typename word>
MemoryPool::MemoryPool(const Parameter<word> &param)
    : base_(static_cast<std::size_t>(0), static_cast<std::size_t>(0)) {
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

void MemoryPool::EnableStatistics() {
  if (!statistics_) {
    statistics_ = std::make_unique<rmm::mr::statistics_resource_adaptor<DefaultUpstream>>(&base_);
  }
  rmm::mr::set_current_device_resource(statistics_.get());
}

MemoryPool::Statistics MemoryPool::GetStatistics() const {
  if (!statistics_) return {0, 0, 0};
  const auto bytes = statistics_->get_bytes_counter();
  const auto calls = statistics_->get_allocations_counter();
  return {bytes.value, bytes.peak, calls.total};
}

template MemoryPool::MemoryPool(const Parameter<uint32_t> &param);
template MemoryPool::MemoryPool(const Parameter<uint64_t> &param);

}  // namespace cheddar
