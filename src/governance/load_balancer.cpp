#include <chmicro/governance/load_balancer.h>

namespace chmicro::governance {

chmicro::Result<Endpoint> RoundRobinLoadBalancer::Pick(std::string_view service, const std::vector<Endpoint>& endpoints) {
    if (endpoints.empty()) {
        return chmicro::Status(chmicro::StatusCode::unavailable, "no endpoints");
    }

    std::lock_guard<std::mutex> lk(mu_);
    auto& cur = rr_[std::string(service)];
    auto idx = cur++ % endpoints.size();
    return endpoints[idx];
}

} // namespace chmicro::governance
