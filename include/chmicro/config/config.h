#pragma once

#include <string>
#include <string_view>

#include <chmicro/core/status.h>

#include <chjson/chjson.hpp>

namespace chmicro::config {

class Config {
public:
    static chmicro::Result<Config> LoadFile(std::string path);

    bool Has(std::string_view key) const;

    chmicro::Result<std::string> GetString(std::string_view key) const;
    chmicro::Result<int> GetInt(std::string_view key) const;

    const chjson::sv_value& raw() const { return doc_.root(); }

private:
    chjson::document doc_;
};

} // namespace chmicro::config
