#include <chmicro/core/log.h>

#include <memory>
#include <mutex>

namespace chmicro::log {
namespace {

std::once_flag g_once;
std::unique_ptr<chlog::logger> g_logger;

} // namespace

chlog::level ParseLevel(std::string_view level) {
    if (level == "trace") return chlog::level::trace;
    if (level == "debug") return chlog::level::debug;
    if (level == "info") return chlog::level::info;
    if (level == "warn" || level == "warning") return chlog::level::warn;
    if (level == "error") return chlog::level::error;
    if (level == "critical") return chlog::level::critical;
    if (level == "off") return chlog::level::off;
    return chlog::level::info;
}

void Init(std::string_view level) {
    std::call_once(g_once, [] {
        chlog::logger_config cfg;
        cfg.name = "chmicro";
        cfg.level = chlog::level::info;
        cfg.pattern = "[{date} {time}.{ms}][{lvl}][tid={tid}] {msg}";
        cfg.async.enabled = false;
        cfg.parallel_sinks = false;

        g_logger = std::make_unique<chlog::logger>(std::move(cfg));
        g_logger->add_sink(std::make_shared<chlog::console_sink>(chlog::console_sink::style::color));
    });

    Get().set_level(ParseLevel(level));
}

chlog::logger& Get() {
    if (!g_logger) {
        Init("info");
    }
    return *g_logger;
}

} // namespace chmicro::log
