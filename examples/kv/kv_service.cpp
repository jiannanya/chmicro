#include <chmicro/core/metrics.h>
#include <chmicro/http/http_server.h>
#include <chmicro/http/router.h>
#include <chmicro/core/log.h>
#include <chmicro/runtime/app.h>

#include <chjson/chjson.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

std::string MakeRequestId() {
    static std::atomic<std::uint64_t> seq{0};
    auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    auto s = seq.fetch_add(1, std::memory_order_relaxed);
    return std::to_string(now) + "-" + std::to_string(s);
}

class ShardedKvStore {
public:
    explicit ShardedKvStore(std::size_t shards)
        : shards_(shards == 0 ? 1 : shards) {
        table_.reserve(shards_);
        for (std::size_t i = 0; i < shards_; ++i) {
            table_.push_back(std::make_unique<Shard>());
        }
    }

    void Put(std::string key, std::string value) {
        auto& shard = *table_[ShardIndex(key)];
        std::unique_lock<std::shared_mutex> lk(shard.mu);
        shard.kv[std::move(key)] = std::move(value);
    }

    bool Get(std::string_view key, std::string& out_value) const {
        auto& shard = *table_[ShardIndex(key)];
        std::shared_lock<std::shared_mutex> lk(shard.mu);
        auto it = shard.kv.find(std::string(key));
        if (it == shard.kv.end()) {
            return false;
        }
        out_value = it->second;
        return true;
    }

    std::size_t Size() const {
        std::size_t total = 0;
        for (const auto& shardp : table_) {
            const auto& shard = *shardp;
            std::shared_lock<std::shared_mutex> lk(shard.mu);
            total += shard.kv.size();
        }
        return total;
    }

private:
    struct Shard {
        mutable std::shared_mutex mu;
        std::unordered_map<std::string, std::string> kv;
    };

    std::size_t ShardIndex(std::string_view key) const {
        return std::hash<std::string_view>{}(key) % shards_;
    }

    std::size_t shards_ = 1;
    std::vector<std::unique_ptr<Shard>> table_;
};

void SetJson(chmicro::http::Response& resp, chjson::value j, unsigned status = 200) {
    resp.status = status;
    resp.SetJson(chjson::dump(j));
}

std::string GetStringOrDefault(const chjson::sv_value& obj, std::string_view key, std::string_view def) {
    const auto* v = obj.find(key);
    if (v == nullptr || !v->is_string()) {
        return std::string(def);
    }
    return std::string(v->as_string_view());
}

void CpuBurn(std::uint64_t iters) {
    volatile std::uint64_t sink = 0;
    std::uint64_t x = 0x9e3779b97f4a7c15ULL;
    for (std::uint64_t i = 0; i < iters; ++i) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        sink ^= x * 0x2545F4914F6CDD1DULL;
    }
}

} // namespace

int main(int argc, char** argv) {
    chmicro::AppOptions opt;
    opt.io_threads = 0;
    opt.log_level = "info";

    chmicro::http::ListenAddress listen{"0.0.0.0", 8087};
    std::size_t shards = 64;
    std::size_t max_value_bytes = 4096;

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
        } else if (a == "--shards" && i + 1 < argc) {
            shards = static_cast<std::size_t>(std::atoi(argv[++i]));
        } else if (a == "--max-value" && i + 1 < argc) {
            max_value_bytes = static_cast<std::size_t>(std::atoi(argv[++i]));
        }
    }

    ShardedKvStore store(shards);

    chmicro::App app(opt);
    chmicro::http::Router r;

    // Middleware: propagate / generate request-id; add a few diagnostic headers.
    r.Use([&](const chmicro::http::Request& req, chmicro::http::Response& resp, chmicro::http::Next next) {
        std::string req_id;
        if (auto it = req.raw.find("x-request-id"); it != req.raw.end()) {
            req_id.assign(it->value().data(), it->value().size());
        } else {
            req_id = MakeRequestId();
        }
        resp.headers["x-request-id"] = req_id;
        resp.headers["x-trace-id"] = req.trace.trace_id;
        resp.headers["x-span-id"] = req.trace.span_id;
        next();
    });

    r.Get("/health", [](const chmicro::http::Request&, chmicro::http::Response& resp) {
        resp.status = 200;
        resp.content_type = "text/plain; charset=utf-8";
        resp.body = "ok";
    });

    r.Get("/stats", [&](const chmicro::http::Request&, chmicro::http::Response& resp) {
        chjson::value j(chjson::value::object{{"keys", chjson::value::integer(static_cast<std::int64_t>(store.Size()))}});
        SetJson(resp, std::move(j));
    });

    // GET /get?key=foo
    r.Get("/get", [&](const chmicro::http::Request& req, chmicro::http::Response& resp) {
        std::string key(req.Query("key"));
        if (key.empty()) {
            SetJson(resp, chjson::value(chjson::value::object{{"error", chjson::value("missing query param: key")}}), 400);
            return;
        }

        std::string value;
        bool ok = store.Get(key, value);
        if (!ok) {
            SetJson(resp, chjson::value(chjson::value::object{{"error", chjson::value("not found")}, {"key", chjson::value(key)}}), 404);
            return;
        }

        chjson::value j(chjson::value::object{
            {"key", chjson::value(key)},
            {"value", chjson::value(value)},
            {"traceparent", chjson::value(req.trace.ToTraceParent())},
        });
        SetJson(resp, std::move(j));
    });

    // POST /put  {"key":"k","value":"v"}
    r.Post("/put", [&](const chmicro::http::Request& req, chmicro::http::Response& resp) {
        auto r = chjson::parse(req.raw.body());
        if (r.err || !r.doc.root().is_object()) {
            SetJson(resp, chjson::value(chjson::value::object{{"error", chjson::value("invalid json")}}), 400);
            return;
        }

        std::string key = GetStringOrDefault(r.doc.root(), "key", "");
        std::string value = GetStringOrDefault(r.doc.root(), "value", "");
        if (key.empty()) {
            SetJson(resp, chjson::value(chjson::value::object{{"error", chjson::value("missing field: key")}}), 400);
            return;
        }
        if (value.size() > max_value_bytes) {
            SetJson(resp, chjson::value(chjson::value::object{
                {"error", chjson::value("value too large")},
                {"max", chjson::value::integer(static_cast<std::int64_t>(max_value_bytes))},
            }), 413);
            return;
        }
        store.Put(std::move(key), std::move(value));
        SetJson(resp, chjson::value(chjson::value::object{{"ok", chjson::value(true)}}));
    });

    // CPU workload endpoint: GET /compute?iters=100000
    r.Get("/compute", [&](const chmicro::http::Request& req, chmicro::http::Response& resp) {
        std::uint64_t iters = 10000;
        if (auto s = req.Query("iters"); !s.empty()) {
            iters = static_cast<std::uint64_t>(std::strtoull(std::string(s).c_str(), nullptr, 10));
        }
        CpuBurn(iters);
        SetJson(resp, chjson::value(chjson::value::object{
            {"ok", chjson::value(true)},
            {"iters", chjson::value::integer(static_cast<std::int64_t>(iters))},
        }));
    });

    r.Get("/metrics", [](const chmicro::http::Request&, chmicro::http::Response& resp) {
        resp.status = 200;
        resp.content_type = "text/plain; version=0.0.4; charset=utf-8";
        resp.body = chmicro::DefaultMetrics().ToPrometheusText();
    });

    auto& ioc = app.Io().Next();
    auto server = std::make_shared<chmicro::http::HttpServer>(ioc, listen, std::move(r));
    app.AddServer(server);

    chmicro::log::info("KV service: http://{}:{} (shards={}, max_value={})", listen.host, listen.port, shards, max_value_bytes);
    chmicro::log::info("Press Ctrl+C to stop.");
    return app.Run();
}
