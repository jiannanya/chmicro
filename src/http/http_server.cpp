#include <chmicro/http/http_server.h>

#include <chmicro/core/metrics.h>
#include <chmicro/core/trace.h>
#include <chmicro/http/types.h>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <chmicro/core/log.h>

#include <chrono>
#include <string_view>

namespace chmicro::http {
namespace {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

std::string_view ExtractPath(std::string_view target) {
    auto q = target.find('?');
    if (q == std::string_view::npos) {
        return target;
    }
    return target.substr(0, q);
}

void ParseQuery(std::string_view target, std::unordered_map<std::string, std::string>& out) {
    auto q = target.find('?');
    if (q == std::string_view::npos || q + 1 >= target.size()) {
        return;
    }

    std::string_view s = target.substr(q + 1);
    while (!s.empty()) {
        auto amp = s.find('&');
        auto part = (amp == std::string_view::npos) ? s : s.substr(0, amp);
        auto eq = part.find('=');
        if (eq != std::string_view::npos) {
            out.emplace(std::string(part.substr(0, eq)), std::string(part.substr(eq + 1)));
        } else if (!part.empty()) {
            out.emplace(std::string(part), "");
        }
        if (amp == std::string_view::npos) {
            break;
        }
        s.remove_prefix(amp + 1);
    }
}

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, Router& router)
        : stream_(std::move(socket)), router_(router) {}

    void Run() {
        Read();
    }

private:
    void Read() {
        req_ = {};
        http::async_read(stream_, buffer_, req_,
            beast::bind_front_handler(&HttpSession::OnRead, shared_from_this()));
    }

    void OnRead(beast::error_code ec, std::size_t) {
        if (ec == http::error::end_of_stream) {
            return DoClose();
        }
        if (ec) {
            return;
        }

        auto start = std::chrono::steady_clock::now();

        Request req;
        req.raw = std::move(req_);
        auto target_sv = std::string_view(req.raw.target().data(), req.raw.target().size());
        req.path = std::string(ExtractPath(target_sv));
        ParseQuery(target_sv, req.query);

        // traceparent
        if (auto it = req.raw.find("traceparent"); it != req.raw.end()) {
            req.trace = chmicro::TraceContext::ParseTraceParent({it->value().data(), it->value().size()});
        }
        if (!req.trace.valid()) {
            req.trace = chmicro::TraceContext::NewRoot();
        }

        Response resp;
        router_.Handle(req, resp);

        http::response<http::string_body> out{http::status(resp.status), req.raw.version()};
        out.keep_alive(req.raw.keep_alive());
        out.set(http::field::server, "chmicro/0.1");
        out.set(http::field::content_type, resp.content_type);
        out.set("traceparent", req.trace.ToTraceParent());
        for (const auto& h : resp.headers) {
            out.set(h.first, h.second);
        }
        out.body() = std::move(resp.body);
        out.prepare_payload();

        auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        {
            auto& hist = chmicro::DefaultMetrics().HistogramMetric(
                "http_server_request_ms",
                "HTTP server request latency (ms)",
                {0.25, 0.5, 1, 2, 5, 10, 25, 50, 100},
                MetricLabels{{{"path", req.path}}});
            hist.Observe(elapsed);
            chmicro::DefaultMetrics().CounterMetric(
                "http_server_requests_total",
                "HTTP server requests total",
                MetricLabels{{{"path", req.path}, {"status", std::to_string(resp.status)}}})
                .Inc(1);
        }

        auto sp = std::make_shared<http::response<http::string_body>>(std::move(out));
        http::async_write(stream_, *sp,
            beast::bind_front_handler(&HttpSession::OnWrite, shared_from_this(), sp->need_eof(), sp));
    }

    void OnWrite(bool close, std::shared_ptr<void>, beast::error_code ec, std::size_t) {
        if (ec) {
            return;
        }
        if (close) {
            return DoClose();
        }
        Read();
    }

    void DoClose() {
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    }

private:
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    Router& router_;
};

} // namespace

std::string_view Request::Query(std::string_view key) const {
    auto it = query.find(std::string(key));
    if (it == query.end()) {
        return {};
    }
    return it->second;
}

void Response::SetJson(std::string json) {
    content_type = "application/json; charset=utf-8";
    body = std::move(json);
}

HttpServer::HttpServer(boost::asio::io_context& ioc, ListenAddress addr, Router router)
    : ioc_(ioc), addr_(std::move(addr)), router_(std::move(router)), acceptor_(ioc) {}

void HttpServer::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    beast::error_code ec;
    auto address = boost::asio::ip::make_address(addr_.host, ec);
    if (ec) {
        chmicro::log::error("invalid listen address {}: {}", addr_.host, ec.message());
        return;
    }
    tcp::endpoint endpoint{address, addr_.port};

    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        chmicro::log::error("acceptor open failed: {}", ec.message());
        return;
    }

    acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
        chmicro::log::warn("acceptor set_option failed: {}", ec.message());
    }

    acceptor_.bind(endpoint, ec);
    if (ec) {
        chmicro::log::error("acceptor bind failed: {}", ec.message());
        return;
    }

    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        chmicro::log::error("acceptor listen failed: {}", ec.message());
        return;
    }

    chmicro::log::info("HTTP server listening on {}:{}", addr_.host, addr_.port);
    DoAccept();
}

void HttpServer::Stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    beast::error_code ec;
    acceptor_.cancel(ec);
    acceptor_.close(ec);
}

void HttpServer::DoAccept() {
    acceptor_.async_accept(boost::asio::make_strand(ioc_),
        [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
            if (ec) {
                if (self->running_.load(std::memory_order_relaxed)) {
                    chmicro::log::warn("accept failed: {}", ec.message());
                    self->DoAccept();
                }
                return;
            }

            std::make_shared<HttpSession>(std::move(socket), self->router_)->Run();
            self->DoAccept();
        });
}

} // namespace chmicro::http
