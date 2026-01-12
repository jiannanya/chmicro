#pragma once

#include <chrono>
#include <string>

#include <chmicro/core/status.h>

namespace chmicro::http {

struct HttpClientResponse {
    int status = 0;
    std::string body;
    std::string content_type;
};

class HttpClient {
public:
    // Thread-safe: each call uses a local io_context.
    static chmicro::Result<HttpClientResponse> Get(
        std::string host,
        std::string port,
        std::string target,
        std::chrono::milliseconds timeout);
};

} // namespace chmicro::http
