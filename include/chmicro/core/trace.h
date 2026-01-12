#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace chmicro {

struct TraceContext {
    // W3C trace-context: traceparent: "00-<trace_id:32hex>-<span_id:16hex>-<flags:2hex>"
    std::string trace_id; // 16 bytes => 32 hex chars
    std::string span_id;  // 8 bytes  => 16 hex chars
    std::string flags;    // 1 byte   => 2 hex chars

    bool valid() const;

    static TraceContext NewRoot();
    static TraceContext NewChild(const TraceContext& parent);

    static TraceContext ParseTraceParent(std::string_view traceparent);
    std::string ToTraceParent() const;
};

} // namespace chmicro
