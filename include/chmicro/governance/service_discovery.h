#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <chmicro/core/status.h>

namespace chmicro::governance {

struct Endpoint {
    std::string host;
    std::uint16_t port = 0;
};

class IServiceDiscovery {
public:
    virtual ~IServiceDiscovery() = default;

    // Thread-safe
    virtual chmicro::Result<std::vector<Endpoint>> Resolve(std::string_view service) const = 0;
};

// A simple in-process registry. Useful for tests / single-process demos.
class InMemoryServiceDiscovery final : public IServiceDiscovery {
public:
    // Requires external synchronization if called concurrently with Resolve()
    void Set(std::string service, std::vector<Endpoint> endpoints);

    chmicro::Result<std::vector<Endpoint>> Resolve(std::string_view service) const override;

private:
    std::map<std::string, std::vector<Endpoint>> table_;
};

} // namespace chmicro::governance
