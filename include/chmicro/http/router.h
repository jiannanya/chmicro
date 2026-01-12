#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/beast/http.hpp>

#include <chmicro/http/types.h>

namespace chmicro::http {

using Handler = std::function<void(const Request&, Response&)>;
using Next = std::function<void()>;
using Middleware = std::function<void(const Request&, Response&, Next)>;

class Router {
public:
    // Thread-safe for read after construction. Build routes before serving.
    void Use(Middleware mw);

    void AddRoute(boost::beast::http::verb method, std::string path, Handler handler);

    void Get(std::string path, Handler handler) { AddRoute(boost::beast::http::verb::get, std::move(path), std::move(handler)); }
    void Post(std::string path, Handler handler) { AddRoute(boost::beast::http::verb::post, std::move(path), std::move(handler)); }

    void Handle(const Request& req, Response& resp) const;

private:
    struct RouteKey {
        boost::beast::http::verb method;
        std::string path;

        bool operator==(const RouteKey& o) const { return method == o.method && path == o.path; }
    };

    struct RouteKeyHash {
        std::size_t operator()(const RouteKey& k) const;
    };

    std::vector<Middleware> middleware_;
    std::unordered_map<RouteKey, Handler, RouteKeyHash> routes_;
};

} // namespace chmicro::http
