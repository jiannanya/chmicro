#include <chmicro/resilience/circuit_breaker.h>

namespace chmicro::resilience {

CircuitBreaker::CircuitBreaker(CircuitBreakerOptions opts) : opts_(opts) {
    if (opts_.consecutive_failures_to_open == 0) {
        // avoid never opening due to 0
        const_cast<CircuitBreakerOptions&>(opts_).consecutive_failures_to_open = 1;
    }
    if (opts_.half_open_max_inflight == 0) {
        const_cast<CircuitBreakerOptions&>(opts_).half_open_max_inflight = 1;
    }
    if (opts_.consecutive_successes_to_close == 0) {
        const_cast<CircuitBreakerOptions&>(opts_).consecutive_successes_to_close = 1;
    }
}

void CircuitBreaker::TryTransitionToHalfOpenLocked(std::chrono::steady_clock::time_point now) {
    if (state_.load(std::memory_order_relaxed) != CircuitState::open) {
        return;
    }
    if (now - opened_at_ < opts_.open_interval) {
        return;
    }

    state_.store(CircuitState::half_open, std::memory_order_release);
    consecutive_failures_ = 0;
    consecutive_successes_ = 0;
    half_open_inflight_ = 0;
}

bool CircuitBreaker::AllowRequest() {
    auto st = state_.load(std::memory_order_acquire);
    if (st == CircuitState::closed) {
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(mu_);

    TryTransitionToHalfOpenLocked(now);

    st = state_.load(std::memory_order_relaxed);
    if (st == CircuitState::open) {
        return false;
    }

    // half-open
    if (half_open_inflight_ >= opts_.half_open_max_inflight) {
        return false;
    }
    ++half_open_inflight_;
    return true;
}

void CircuitBreaker::OnSuccess() {
    std::lock_guard<std::mutex> lk(mu_);

    auto st = state_.load(std::memory_order_relaxed);
    if (st == CircuitState::closed) {
        consecutive_failures_ = 0;
        return;
    }

    if (st == CircuitState::half_open) {
        if (half_open_inflight_ > 0) {
            --half_open_inflight_;
        }
        ++consecutive_successes_;
        if (consecutive_successes_ >= opts_.consecutive_successes_to_close) {
            state_.store(CircuitState::closed, std::memory_order_release);
            consecutive_failures_ = 0;
            consecutive_successes_ = 0;
            half_open_inflight_ = 0;
        }
        return;
    }

    // open
    // ignore success callbacks in open state
}

void CircuitBreaker::OnFailure() {
    std::lock_guard<std::mutex> lk(mu_);

    auto st = state_.load(std::memory_order_relaxed);
    if (st == CircuitState::closed) {
        ++consecutive_failures_;
        if (consecutive_failures_ >= opts_.consecutive_failures_to_open) {
            state_.store(CircuitState::open, std::memory_order_release);
            opened_at_ = std::chrono::steady_clock::now();
            consecutive_successes_ = 0;
        }
        return;
    }

    if (st == CircuitState::half_open) {
        if (half_open_inflight_ > 0) {
            --half_open_inflight_;
        }
        // any failure in half-open -> open
        state_.store(CircuitState::open, std::memory_order_release);
        opened_at_ = std::chrono::steady_clock::now();
        consecutive_failures_ = 0;
        consecutive_successes_ = 0;
        half_open_inflight_ = 0;
        return;
    }

    // open
    // keep open
}

} // namespace chmicro::resilience
