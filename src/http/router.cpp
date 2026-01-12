#include <chmicro/http/router.h>

#include <boost/functional/hash.hpp>

namespace chmicro::http {

std::size_t Router::RouteKeyHash::operator()(const RouteKey& k) const {
    std::size_t seed = 0;
    boost::hash_combine(seed, static_cast<unsigned>(k.method));
    boost::hash_combine(seed, k.path);
    return seed;
}

void Router::Use(Middleware mw) {
    middleware_.push_back(std::move(mw));
}

void Router::AddRoute(boost::beast::http::verb method, std::string path, Handler handler) {
    routes_[RouteKey{method, std::move(path)}] = std::move(handler);
}

void Router::Handle(const Request& req, Response& resp) const {
    auto it = routes_.find(RouteKey{req.raw.method(), req.path});
    if (it == routes_.end()) {
        resp.status = 404;
        resp.content_type = "application/json; charset=utf-8";
        resp.body = "{\"error\":\"not_found\"}";
        return;
    }

    const auto& handler = it->second;

    // Build middleware chain.
    std::size_t idx = 0;
    std::function<void()> run;
    run = [&]() {
        if (idx < middleware_.size()) {
            auto& mw = middleware_[idx++];
            mw(req, resp, run);
            return;
        }
        handler(req, resp);
    };

    run();
}

} // namespace chmicro::http
