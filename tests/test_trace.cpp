#include <chtest.hpp>

#include <chmicro/core/trace.h>

TEST_CASE("TraceContext generates valid traceparent") {
    auto ctx = chmicro::TraceContext::NewRoot();
    REQUIRE(ctx.valid());

    auto tp = ctx.ToTraceParent();
    REQUIRE(tp.size() == 55);

    auto parsed = chmicro::TraceContext::ParseTraceParent(tp);
    REQUIRE(parsed.valid());
    REQUIRE(parsed.trace_id == ctx.trace_id);
    REQUIRE(parsed.flags == ctx.flags);
}

TEST_CASE("TraceContext child shares trace_id") {
    auto root = chmicro::TraceContext::NewRoot();
    auto child = chmicro::TraceContext::NewChild(root);

    REQUIRE(child.valid());
    REQUIRE(child.trace_id == root.trace_id);
    REQUIRE(child.span_id != root.span_id);
}
