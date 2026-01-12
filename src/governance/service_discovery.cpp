#include <chmicro/governance/service_discovery.h>

namespace chmicro::governance {

void InMemoryServiceDiscovery::Set(std::string service, std::vector<Endpoint> endpoints) {
    table_[std::move(service)] = std::move(endpoints);
}

chmicro::Result<std::vector<Endpoint>> InMemoryServiceDiscovery::Resolve(std::string_view service) const {
    auto it = table_.find(std::string(service));
    if (it == table_.end()) {
        return chmicro::Status(chmicro::StatusCode::not_found, "service not found");
    }
    return it->second;
}

} // namespace chmicro::governance
