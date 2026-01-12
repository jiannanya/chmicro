#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace chmicro::resilience {

enum class CircuitState {
    closed = 0,
    open,
    half_open,
};

struct CircuitBreakerOptions {
    std::uint32_t consecutive_failures_to_open = 5;
    std::chrono::milliseconds open_interval{2000};

    std::uint32_t half_open_max_inflight = 1;
    std::uint32_t consecutive_successes_to_close = 2;
};

class CircuitBreaker {
public:
    explicit CircuitBreaker(CircuitBreakerOptions opts);

    // Thread-safe
    bool AllowRequest();

    // Thread-safe
    void OnSuccess();

    // Thread-safe
    void OnFailure();

    CircuitState state() const { return state_.load(std::memory_order_acquire); }

private:
    void TryTransitionToHalfOpenLocked(std::chrono::steady_clock::time_point now);

    const CircuitBreakerOptions opts_;

    mutable std::mutex mu_;
    std::atomic<CircuitState> state_{CircuitState::closed};

    std::uint32_t consecutive_failures_ = 0;
    std::uint32_t consecutive_successes_ = 0;

    std::chrono::steady_clock::time_point opened_at_{};
    std::uint32_t half_open_inflight_ = 0;
};

} // namespace chmicro::resilience
