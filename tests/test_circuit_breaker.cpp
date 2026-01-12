#include <chtest.hpp>

#include <chmicro/resilience/circuit_breaker.h>

#include <thread>

using chmicro::resilience::CircuitBreaker;
using chmicro::resilience::CircuitBreakerOptions;
using chmicro::resilience::CircuitState;

TEST_CASE("CircuitBreaker opens after consecutive failures") {
    CircuitBreakerOptions opt;
    opt.consecutive_failures_to_open = 3;
    opt.open_interval = std::chrono::milliseconds(100);

    CircuitBreaker cb(opt);

    REQUIRE(cb.state() == CircuitState::closed);
    REQUIRE(cb.AllowRequest());
    cb.OnFailure();
    REQUIRE(cb.AllowRequest());
    cb.OnFailure();
    REQUIRE(cb.AllowRequest());
    cb.OnFailure();

    REQUIRE(cb.state() == CircuitState::open);
    REQUIRE(!cb.AllowRequest());
}

TEST_CASE("CircuitBreaker half-open then closes on successes") {
    CircuitBreakerOptions opt;
    opt.consecutive_failures_to_open = 1;
    opt.open_interval = std::chrono::milliseconds(10);
    opt.consecutive_successes_to_close = 2;

    CircuitBreaker cb(opt);

    REQUIRE(cb.AllowRequest());
    cb.OnFailure();
    REQUIRE(cb.state() == CircuitState::open);

    std::this_thread::sleep_for(opt.open_interval + std::chrono::milliseconds(5));

    REQUIRE(cb.AllowRequest());
    REQUIRE(cb.state() == CircuitState::half_open);
    cb.OnSuccess();

    REQUIRE(cb.AllowRequest());
    cb.OnSuccess();

    REQUIRE(cb.state() == CircuitState::closed);
}
