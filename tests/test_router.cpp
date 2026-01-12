#include <chtest.hpp>

#include <chmicro/http/router.h>

TEST_CASE("Router routes exact path") {
    chmicro::http::Router r;
    bool called = false;

    r.Get("/health", [&](const chmicro::http::Request&, chmicro::http::Response& resp) {
        called = true;
        resp.status = 200;
        resp.body = "ok";
    });

    chmicro::http::Request req;
    req.raw.method(boost::beast::http::verb::get);
    req.path = "/health";

    chmicro::http::Response resp;
    r.Handle(req, resp);

    REQUIRE(called);
    REQUIRE(resp.status == 200);
    REQUIRE(resp.body == "ok");
}

TEST_CASE("Router returns 404 when missing") {
    chmicro::http::Router r;

    chmicro::http::Request req;
    req.raw.method(boost::beast::http::verb::get);
    req.path = "/missing";

    chmicro::http::Response resp;
    r.Handle(req, resp);

    REQUIRE(resp.status == 404);
}
