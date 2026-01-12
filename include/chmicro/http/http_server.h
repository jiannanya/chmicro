#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <boost/asio/ip/tcp.hpp>

#include <chmicro/runtime/app.h>
#include <chmicro/http/router.h>

namespace chmicro::http {

struct ListenAddress {
    std::string host;
    std::uint16_t port = 0;
};

class HttpServer final : public chmicro::IHttpServer, public std::enable_shared_from_this<HttpServer> {
public:
    HttpServer(boost::asio::io_context& ioc, ListenAddress addr, Router router);

    void Start() override;
    void Stop() override;

private:
    void DoAccept();

    boost::asio::io_context& ioc_;
    ListenAddress addr_;
    Router router_;

    boost::asio::ip::tcp::acceptor acceptor_;
    std::atomic<bool> running_{false};
};

} // namespace chmicro::http
