#pragma once

#include <atomic>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <chmicro/core/status.h>
#include <chmicro/governance/service_discovery.h>

namespace chmicro::governance {

class ILoadBalancer {
public:
    virtual ~ILoadBalancer() = default;

    // Thread-safe
    virtual chmicro::Result<Endpoint> Pick(std::string_view service, const std::vector<Endpoint>& endpoints) = 0;
};

class RoundRobinLoadBalancer final : public ILoadBalancer {
public:
    // Thread-safe
    chmicro::Result<Endpoint> Pick(std::string_view service, const std::vector<Endpoint>& endpoints) override;

private:
    std::mutex mu_;
    std::map<std::string, std::size_t> rr_; // per-service cursor
};

} // namespace chmicro::governance
