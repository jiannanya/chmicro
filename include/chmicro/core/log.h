#pragma once

#include <string_view>
#include <utility>

#include <chlog/chlog.hpp>

namespace chmicro::log {

// Thread-safe
void Init(std::string_view level);

// Thread-safe after Init(); always returns a valid logger.
chlog::logger& Get();

chlog::level ParseLevel(std::string_view level);

template <class... Args>
inline void info(std::format_string<Args...> fmt, Args&&... args) {
    Get().info(fmt, std::forward<Args>(args)...);
}

template <class... Args>
inline void warn(std::format_string<Args...> fmt, Args&&... args) {
    Get().warn(fmt, std::forward<Args>(args)...);
}

template <class... Args>
inline void error(std::format_string<Args...> fmt, Args&&... args) {
    Get().error(fmt, std::forward<Args>(args)...);
}

} // namespace chmicro::log
