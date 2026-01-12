#include <chmicro/resilience/retry.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace chmicro::resilience {

RetryPolicy::RetryPolicy(RetryOptions opts) : opts_(opts) {
    if (opts_.max_attempts < 1) {
        opts_.max_attempts = 1;
    }
    if (opts_.jitter_ratio < 0.0) {
        opts_.jitter_ratio = 0.0;
    }
    if (opts_.jitter_ratio > 1.0) {
        opts_.jitter_ratio = 1.0;
    }
}

std::chrono::milliseconds RetryPolicy::BackoffBeforeAttempt(int attempt) const {
    if (attempt <= 1) {
        return std::chrono::milliseconds(0);
    }

    // Exponential backoff: base * 2^(attempt-2)
    auto exp = attempt - 2;
    double factor = std::pow(2.0, static_cast<double>(exp));
    auto raw = static_cast<long long>(opts_.base_backoff.count() * factor);
    raw = std::min<long long>(raw, opts_.max_backoff.count());

    // Full jitter in [-jitter, +jitter]
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(-opts_.jitter_ratio, opts_.jitter_ratio);
    double jitter = dist(gen);

    auto jittered = static_cast<long long>(raw * (1.0 + jitter));
    jittered = std::max<long long>(0, jittered);
    jittered = std::min<long long>(jittered, opts_.max_backoff.count());

    return std::chrono::milliseconds(jittered);
}

} // namespace chmicro::resilience
