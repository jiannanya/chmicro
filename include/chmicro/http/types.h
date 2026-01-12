#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include <boost/beast/http.hpp>

#include <chmicro/core/trace.h>

namespace chmicro::http {

namespace beast_http = boost::beast::http;

struct Request {
    beast_http::request<beast_http::string_body> raw;
    std::string path; // target without query
    std::unordered_map<std::string, std::string> query;
    chmicro::TraceContext trace;

    std::string_view Query(std::string_view key) const;
};

struct Response {
    unsigned status = 200;
    std::string body;
    std::string content_type = "text/plain; charset=utf-8";
    std::unordered_map<std::string, std::string> headers;

    void SetJson(std::string json);
};

} // namespace chmicro::http
