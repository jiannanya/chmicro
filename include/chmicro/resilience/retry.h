#pragma once

#include <chrono>
#include <cstdint>

namespace chmicro::resilience {

struct RetryOptions {
    int max_attempts = 3;
    std::chrono::milliseconds base_backoff{5};
    std::chrono::milliseconds max_backoff{200};
    double jitter_ratio = 0.2; // [0,1]
};

class RetryPolicy {
public:
    explicit RetryPolicy(RetryOptions opts);

    int max_attempts() const { return opts_.max_attempts; }

    // attempt: 1..max_attempts, returns sleep duration before the attempt (attempt=1 returns 0)
    std::chrono::milliseconds BackoffBeforeAttempt(int attempt) const;

private:
    RetryOptions opts_;
};

} // namespace chmicro::resilience
