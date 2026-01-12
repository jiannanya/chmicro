#include <chmicro/http/http_client.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace chmicro::http {
namespace {
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

struct ClientOpState {
    boost::asio::io_context ioc;
    tcp::resolver resolver{ioc};
    beast::tcp_stream stream{ioc};
    boost::asio::steady_timer timer{ioc};
    beast::flat_buffer buffer;
    http::request<http::empty_body> req;
    http::response<http::string_body> resp;
    beast::error_code ec;
    bool timed_out = false;
};

} // namespace

chmicro::Result<HttpClientResponse> HttpClient::Get(std::string host, std::string port, std::string target, std::chrono::milliseconds timeout) {
    ClientOpState st;
    st.req.method(http::verb::get);
    st.req.version(11);
    st.req.target(target);
    st.req.set(http::field::host, host);
    st.req.set(http::field::user_agent, "chmicro/0.1");

    st.timer.expires_after(timeout);
    st.timer.async_wait([&](beast::error_code ec) {
        if (ec) {
            return;
        }
        st.timed_out = true;
        st.stream.cancel();
    });

    st.resolver.async_resolve(host, port, [&](beast::error_code ec, tcp::resolver::results_type results) {
        if (ec) {
            st.ec = ec;
            return;
        }
        st.stream.async_connect(results, [&](beast::error_code ec, const tcp::resolver::results_type::endpoint_type&) {
            if (ec) {
                st.ec = ec;
                return;
            }
            http::async_write(st.stream, st.req, [&](beast::error_code ec, std::size_t) {
                if (ec) {
                    st.ec = ec;
                    return;
                }
                http::async_read(st.stream, st.buffer, st.resp, [&](beast::error_code ec, std::size_t) {
                    if (ec) {
                        st.ec = ec;
                        return;
                    }
                    beast::error_code ec2;
                    st.stream.socket().shutdown(tcp::socket::shutdown_both, ec2);
                });
            });
        });
    });

    st.ioc.run();

    if (st.timed_out) {
        return chmicro::Status(chmicro::StatusCode::timeout, "http client timeout");
    }
    if (st.ec) {
        return chmicro::Status(chmicro::StatusCode::unavailable, st.ec.message());
    }

    HttpClientResponse out;
    out.status = static_cast<int>(st.resp.result_int());
    out.body = st.resp.body();
    if (auto it = st.resp.find(http::field::content_type); it != st.resp.end()) {
        out.content_type = std::string(it->value().data(), it->value().size());
    }
    return out;
}

} // namespace chmicro::http
