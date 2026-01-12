#include <chmicro/core/trace.h>

#include <algorithm>
#include <array>
#include <random>

namespace chmicro {
namespace {

bool IsLowerHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

std::string BytesToLowerHex(const uint8_t* data, size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out[i * 2 + 0] = kHex[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[(data[i] >> 0) & 0xF];
    }
    return out;
}

std::string RandomHex(size_t bytes) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 255);

    std::string out;
    out.resize(bytes * 2);
    static const char* kHex = "0123456789abcdef";
    for (size_t i = 0; i < bytes; ++i) {
        uint8_t b = static_cast<uint8_t>(dist(gen));
        out[i * 2 + 0] = kHex[(b >> 4) & 0xF];
        out[i * 2 + 1] = kHex[(b >> 0) & 0xF];
    }
    return out;
}

} // namespace

bool TraceContext::valid() const {
    if (trace_id.size() != 32 || span_id.size() != 16 || flags.size() != 2) {
        return false;
    }
    if (!std::all_of(trace_id.begin(), trace_id.end(), IsLowerHex)) {
        return false;
    }
    if (!std::all_of(span_id.begin(), span_id.end(), IsLowerHex)) {
        return false;
    }
    if (!std::all_of(flags.begin(), flags.end(), IsLowerHex)) {
        return false;
    }
    // Disallow all-zero ids per spec spirit
    if (std::all_of(trace_id.begin(), trace_id.end(), [](char c) { return c == '0'; })) {
        return false;
    }
    if (std::all_of(span_id.begin(), span_id.end(), [](char c) { return c == '0'; })) {
        return false;
    }
    return true;
}

TraceContext TraceContext::NewRoot() {
    TraceContext ctx;
    ctx.trace_id = RandomHex(16);
    ctx.span_id = RandomHex(8);
    ctx.flags = "01"; // sampled by default
    return ctx;
}

TraceContext TraceContext::NewChild(const TraceContext& parent) {
    TraceContext ctx;
    ctx.trace_id = parent.trace_id;
    ctx.span_id = RandomHex(8);
    ctx.flags = parent.flags.empty() ? "01" : parent.flags;
    if (!ctx.valid()) {
        return NewRoot();
    }
    return ctx;
}

TraceContext TraceContext::ParseTraceParent(std::string_view traceparent) {
    // Format: version(2) '-' trace_id(32) '-' span_id(16) '-' flags(2)
    TraceContext ctx;

    if (traceparent.size() != 55) {
        return ctx;
    }
    if (traceparent[2] != '-' || traceparent[35] != '-' || traceparent[52] != '-') {
        return ctx;
    }

    // Only accept lowercase hex for simplicity (we generate lowercase)
    auto ver = traceparent.substr(0, 2);
    if (!std::all_of(ver.begin(), ver.end(), IsLowerHex)) {
        return ctx;
    }

    ctx.trace_id = std::string(traceparent.substr(3, 32));
    ctx.span_id = std::string(traceparent.substr(36, 16));
    ctx.flags = std::string(traceparent.substr(53, 2));

    if (!ctx.valid()) {
        return TraceContext{};
    }
    return ctx;
}

std::string TraceContext::ToTraceParent() const {
    if (!valid()) {
        return {};
    }
    return "00-" + trace_id + "-" + span_id + "-" + flags;
}

} // namespace chmicro
