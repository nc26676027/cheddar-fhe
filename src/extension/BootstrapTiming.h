#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "common/Assert.h"

namespace cheddar::detail {

// Private timing mechanics shared by coarse markers and detailed observations.
class BootstrapTiming {
 public:
  using Marker = std::function<void(const char *)>;

  template <typename Timing>
  static Timing Measure(const std::function<void(const Marker &)> &evaluate) {
    BootstrapTiming timer;
    Timing result{};
    timer.Begin();
    const Marker mark = [&](const char *name) {
      const double elapsed = timer.Mark();
      result.stages.push_back({name, 0, elapsed});
    };
    evaluate(mark);
    mark("ReleaseTemps");
    Check(cudaEventSynchronize(timer.events_[timer.cursor_]->value),
          "evaluation synchronization");
    result.wall_ms = Milliseconds(Clock::now() - timer.begin_);
    Check(cudaEventElapsedTime(&result.gpu_ms, timer.events_[0]->value,
                               timer.events_[timer.cursor_]->value),
          "total GPU timing");
    for (size_t i = 0; i < result.stages.size(); ++i) {
      Check(cudaEventElapsedTime(&result.stages[i].gpu_ms,
                                 timer.events_[i]->value,
                                 timer.events_[i + 1]->value),
            "stage GPU timing");
    }
    return result;
  }

 private:
  using Clock = std::chrono::steady_clock;

  static void Check(cudaError_t status, const char *operation) {
    if (status != cudaSuccess) {
      throw std::runtime_error(std::string("Bootstrap profiling: ") + operation +
                               ": " + cudaGetErrorString(status));
    }
  }

  struct Event {
    cudaEvent_t value = nullptr;
    Event() { Check(cudaEventCreate(&value), "event creation"); }
    Event(const Event &) = delete;
    Event &operator=(const Event &) = delete;
    ~Event() {
      if (value)
        CheckTrue(cudaEventDestroy(value) == cudaSuccess,
                  "Bootstrap profiling event destruction failed");
    }
  };

  BootstrapTiming() {
    events_.reserve(64);
    for (size_t i = 0; i < 64; ++i)
      events_.push_back(std::make_unique<Event>());
  }

  static double Milliseconds(Clock::duration elapsed) {
    return std::chrono::duration<double, std::milli>(elapsed).count();
  }

  void Begin() {
    Check(cudaDeviceSynchronize(), "initial synchronization");
    begin_ = previous_ = Clock::now();
    Check(cudaEventRecord(events_[0]->value), "initial event recording");
  }

  double Mark() {
    if (cursor_ + 1 == events_.size())
      events_.push_back(std::make_unique<Event>());
    Check(cudaEventRecord(events_[++cursor_]->value), "stage event recording");
    const auto now = Clock::now();
    const double elapsed = Milliseconds(now - previous_);
    previous_ = now;
    return elapsed;
  }

  std::vector<std::unique_ptr<Event>> events_;
  size_t cursor_ = 0;
  Clock::time_point begin_, previous_;
};

}  // namespace cheddar::detail
