#include <chmicro/core/metrics.h>
#include <chmicro/http/http_server.h>
#include <chmicro/http/router.h>
#include <chmicro/core/log.h>
#include <chmicro/runtime/app.h>

#include <chjson/chjson.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

bool ParseListen(std::string_view s, chmicro::http::ListenAddress& out) {
    auto colon = s.rfind(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    out.host = std::string(s.substr(0, colon));
    auto port_sv = s.substr(colon + 1);
    if (out.host.empty() || port_sv.empty()) {
        return false;
    }
    int port = std::atoi(std::string(port_sv).c_str());
    if (port <= 0 || port > 65535) {
        return false;
    }
    out.port = static_cast<std::uint16_t>(port);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    chmicro::AppOptions opt;
    opt.io_threads = 0;
    opt.log_level = "info";

    chmicro::http::ListenAddress listen{"0.0.0.0", 8086};

    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        if (a == "--threads" && i + 1 < argc) {
            opt.io_threads = static_cast<std::size_t>(std::atoi(argv[++i]));
        } else if (a == "--listen" && i + 1 < argc) {
            if (!ParseListen(argv[++i], listen)) {
                std::cerr << "Invalid --listen, expected host:port\n";
                return 2;
            }
        } else if (a == "--log" && i + 1 < argc) {
            opt.log_level = argv[++i];
        }
    }

    chmicro::App app(opt);

    chmicro::http::Router r;

    r.Get("/health", [](const chmicro::http::Request&, chmicro::http::Response& resp) {
        resp.status = 200;
        resp.content_type = "text/plain; charset=utf-8";
        resp.body = "ok";
    });

    r.Get("/hello", [](const chmicro::http::Request& req, chmicro::http::Response& resp) {
        std::string name(req.Query("name"));
        if (name.empty()) {
            name = "world";
        }

        chjson::value j(chjson::value::object{
            {"message", chjson::value(std::string("hello, ") + name)},
            {"traceparent", chjson::value(req.trace.ToTraceParent())},
        });
        resp.SetJson(chjson::dump(j));
    });

    r.Get("/metrics", [](const chmicro::http::Request&, chmicro::http::Response& resp) {
        resp.status = 200;
        resp.content_type = "text/plain; version=0.0.4; charset=utf-8";
        resp.body = chmicro::DefaultMetrics().ToPrometheusText();
    });

    auto& ioc = app.Io().Next();
    auto server = std::make_shared<chmicro::http::HttpServer>(ioc, listen, std::move(r));
    app.AddServer(server);

    chmicro::log::info("Press Ctrl+C to stop.");
    return app.Run();
}
