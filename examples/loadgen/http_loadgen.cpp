#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace {

struct Options {
    std::string host = "127.0.0.1";
    std::string port = "8087";
    std::string target = "/get?key=hot";

    std::size_t threads = 4;
    std::size_t concurrency = 128;

    int warmup_seconds = 2;
    int duration_seconds = 10;

    int timeout_ms = 1000;
    bool keepalive = true;
};

int Log2FloorU64(std::uint64_t x) {
    if (x == 0) {
        return -1;
    }
#ifdef _MSC_VER
    unsigned long idx = 0;
    _BitScanReverse64(&idx, x);
    return static_cast<int>(idx);
#else
    return 63 - __builtin_clzll(x);
#endif
}

class LatencyHistogram {
public:
    static constexpr std::size_t kBuckets = 64; // log2(us)

    void Reset() {
        for (auto& b : buckets_) {
            b.store(0, std::memory_order_relaxed);
        }
        ok_.store(0, std::memory_order_relaxed);
        err_.store(0, std::memory_order_relaxed);
        bytes_.store(0, std::memory_order_relaxed);
    }

    void RecordOk(std::uint64_t latency_us, std::uint64_t bytes_in) {
        ok_.fetch_add(1, std::memory_order_relaxed);
        bytes_.fetch_add(bytes_in, std::memory_order_relaxed);

        // Bucket by log2(latency_us + 1)
        auto v = latency_us + 1;
        int idx = Log2FloorU64(v);
        if (idx < 0) {
            idx = 0;
        }
        if (idx >= static_cast<int>(kBuckets)) {
            idx = static_cast<int>(kBuckets - 1);
        }
        buckets_[static_cast<std::size_t>(idx)].fetch_add(1, std::memory_order_relaxed);
    }

    void RecordErr() {
        err_.fetch_add(1, std::memory_order_relaxed);
    }

    struct Snapshot {
        std::uint64_t ok = 0;
        std::uint64_t err = 0;
        std::uint64_t bytes = 0;
        std::vector<std::uint64_t> buckets;
    };

    Snapshot Get() const {
        Snapshot s;
        s.ok = ok_.load(std::memory_order_relaxed);
        s.err = err_.load(std::memory_order_relaxed);
        s.bytes = bytes_.load(std::memory_order_relaxed);
        s.buckets.resize(kBuckets);
        for (std::size_t i = 0; i < kBuckets; ++i) {
            s.buckets[i] = buckets_[i].load(std::memory_order_relaxed);
        }
        return s;
    }

    static std::uint64_t ApproxPercentileUs(const Snapshot& s, double p) {
        if (s.ok == 0) {
            return 0;
        }
        std::uint64_t rank = static_cast<std::uint64_t>(static_cast<long double>(s.ok - 1) * p);
        std::uint64_t cum = 0;
        for (std::size_t i = 0; i < s.buckets.size(); ++i) {
            cum += s.buckets[i];
            if (cum > rank) {
                // Approx upper bound of this bucket: 2^i us
                return (i >= 63) ? (1ULL << 63) : (1ULL << i);
            }
        }
        return (1ULL << (kBuckets - 1));
    }

private:
    std::atomic<std::uint64_t> ok_{0};
    std::atomic<std::uint64_t> err_{0};
    std::atomic<std::uint64_t> bytes_{0};
    std::array<std::atomic<std::uint64_t>, kBuckets> buckets_{};
};

class LoadSession : public std::enable_shared_from_this<LoadSession> {
public:
    LoadSession(asio::io_context& ioc,
        Options opt,
        std::shared_ptr<std::atomic<bool>> stop,
        std::shared_ptr<std::atomic<std::uint64_t>> stop_at_ns,
        std::shared_ptr<LatencyHistogram> hist)
                : opt_(std::move(opt)),
          stop_(std::move(stop)),
          stop_at_ns_(std::move(stop_at_ns)),
          hist_(std::move(hist)),
          resolver_(ioc),
          stream_(ioc),
          timer_(ioc) {}

    void Start() {
        Resolve();
    }

private:
    static std::uint64_t NowNs() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    bool ShouldStop() const {
        if (stop_ && stop_->load(std::memory_order_relaxed)) {
            return true;
        }
        if (!stop_at_ns_) {
            return false;
        }
        return NowNs() >= stop_at_ns_->load(std::memory_order_relaxed);
    }

    void Resolve() {
        if (ShouldStop()) {
            return;
        }
        resolver_.async_resolve(opt_.host, opt_.port,
            beast::bind_front_handler(&LoadSession::OnResolve, shared_from_this()));
    }

    void OnResolve(beast::error_code ec, tcp::resolver::results_type results) {
        if (ec) {
            hist_->RecordErr();
            return ReconnectSoon();
        }

        stream_.expires_after(std::chrono::milliseconds(opt_.timeout_ms));
        stream_.async_connect(results, beast::bind_front_handler(&LoadSession::OnConnect, shared_from_this()));
    }

    void OnConnect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
        if (ec) {
            hist_->RecordErr();
            return ReconnectSoon();
        }

        // Build request template.
        req_.version(11);
        req_.method(http::verb::get);
        req_.target(opt_.target);
        req_.set(http::field::host, opt_.host);
        req_.set(http::field::user_agent, "chmicro_loadgen/0.1");
        req_.keep_alive(opt_.keepalive);

        DoRequest();
    }

    void DoRequest() {
        if (ShouldStop()) {
            Close();
            return;
        }

        // Reset response state.
        res_ = {};
        buffer_.consume(buffer_.size());

        start_ns_ = NowNs();

        timer_.expires_after(std::chrono::milliseconds(opt_.timeout_ms));
        timer_.async_wait(beast::bind_front_handler(&LoadSession::OnTimeout, shared_from_this()));

        http::async_write(stream_, req_, beast::bind_front_handler(&LoadSession::OnWrite, shared_from_this()));
    }

    void OnTimeout(beast::error_code ec) {
        if (ec == asio::error::operation_aborted) {
            return;
        }
        // Request timed out.
        hist_->RecordErr();
        beast::error_code ignored;
        stream_.socket().close(ignored);
    }

    void OnWrite(beast::error_code ec, std::size_t) {
        if (ec) {
            timer_.cancel();
            hist_->RecordErr();
            return ReconnectSoon();
        }

        http::async_read(stream_, buffer_, res_, beast::bind_front_handler(&LoadSession::OnRead, shared_from_this()));
    }

    void OnRead(beast::error_code ec, std::size_t bytes_transferred) {
        timer_.cancel();

        if (ec) {
            hist_->RecordErr();
            return ReconnectSoon();
        }

        auto end_ns = NowNs();
        auto latency_us = (end_ns - start_ns_) / 1000;
        hist_->RecordOk(latency_us, static_cast<std::uint64_t>(bytes_transferred));

        // Continue on the same connection (keep-alive). If server closed, reconnect.
        if (!opt_.keepalive || res_.need_eof()) {
            return ReconnectSoon();
        }

        DoRequest();
    }

    void ReconnectSoon() {
        Close();
        if (ShouldStop()) {
            return;
        }
        timer_.expires_after(std::chrono::milliseconds(50));
        timer_.async_wait(beast::bind_front_handler(&LoadSession::OnReconnectTimer, shared_from_this()));
    }

    void OnReconnectTimer(beast::error_code ec) {
        if (ec == asio::error::operation_aborted) {
            return;
        }
        Resolve();
    }

    void Close() {
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
        stream_.socket().close(ec);
    }

private:
    Options opt_;
    std::shared_ptr<std::atomic<bool>> stop_;
    std::shared_ptr<std::atomic<std::uint64_t>> stop_at_ns_;
    std::shared_ptr<LatencyHistogram> hist_;

    tcp::resolver resolver_;
    beast::tcp_stream stream_;
    asio::steady_timer timer_;

    beast::flat_buffer buffer_;
    http::request<http::empty_body> req_;
    http::response<http::string_body> res_;

    std::uint64_t start_ns_ = 0;
};

void PrintUsage() {
    std::cout << "chmicro_loadgen options:\n"
              << "  --host <host>\n"
              << "  --port <port>\n"
              << "  --target <path?query>\n"
              << "  --threads <n>\n"
              << "  --concurrency <n>\n"
              << "  --warmup <seconds>\n"
              << "  --duration <seconds>\n"
              << "  --timeout-ms <ms>\n";
}

} // namespace

int main(int argc, char** argv) {
    Options opt;

    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (a == "--host") {
            opt.host = need("--host");
        } else if (a == "--port") {
            opt.port = need("--port");
        } else if (a == "--target") {
            opt.target = need("--target");
        } else if (a == "--threads") {
            opt.threads = static_cast<std::size_t>(std::atoi(need("--threads")));
        } else if (a == "--concurrency") {
            opt.concurrency = static_cast<std::size_t>(std::atoi(need("--concurrency")));
        } else if (a == "--warmup") {
            opt.warmup_seconds = std::atoi(need("--warmup"));
        } else if (a == "--duration") {
            opt.duration_seconds = std::atoi(need("--duration"));
        } else if (a == "--timeout-ms") {
            opt.timeout_ms = std::atoi(need("--timeout-ms"));
        } else if (a == "--help" || a == "-h") {
            PrintUsage();
            return 0;
        }
    }

    if (opt.threads == 0) {
        opt.threads = 1;
    }
    if (opt.concurrency == 0) {
        opt.concurrency = 1;
    }
    if (opt.duration_seconds <= 0) {
        opt.duration_seconds = 10;
    }

    asio::io_context ioc;

    auto stop = std::make_shared<std::atomic<bool>>(false);
    auto stop_at_ns = std::make_shared<std::atomic<std::uint64_t>>(0);
    auto hist = std::make_shared<LatencyHistogram>();

    // Warmup phase.
    if (opt.warmup_seconds > 0) {
        hist->Reset();
        stop_at_ns->store(static_cast<std::uint64_t>(
                             std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())
                             + static_cast<std::uint64_t>(opt.warmup_seconds) * 1000ULL * 1000ULL * 1000ULL,
            std::memory_order_relaxed);

        for (std::size_t i = 0; i < opt.concurrency; ++i) {
            std::make_shared<LoadSession>(ioc, opt, stop, stop_at_ns, hist)->Start();
        }

        std::vector<std::thread> threads;
        threads.reserve(opt.threads);
        for (std::size_t t = 0; t < opt.threads; ++t) {
            threads.emplace_back([&] { ioc.run(); });
        }

        std::this_thread::sleep_for(std::chrono::seconds(opt.warmup_seconds));
        // Stop warmup sessions by letting them see stop_at.
        ioc.stop();
        for (auto& th : threads) {
            th.join();
        }

        // Reset for measured run.
        ioc.restart();
    }

    hist->Reset();
    auto start = std::chrono::steady_clock::now();
    stop_at_ns->store(static_cast<std::uint64_t>(
                         std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())
                         + static_cast<std::uint64_t>(opt.duration_seconds) * 1000ULL * 1000ULL * 1000ULL,
        std::memory_order_relaxed);

    for (std::size_t i = 0; i < opt.concurrency; ++i) {
        std::make_shared<LoadSession>(ioc, opt, stop, stop_at_ns, hist)->Start();
    }

    std::vector<std::thread> threads;
    threads.reserve(opt.threads);
    for (std::size_t t = 0; t < opt.threads; ++t) {
        threads.emplace_back([&] { ioc.run(); });
    }

    // Wait for duration.
    std::this_thread::sleep_for(std::chrono::seconds(opt.duration_seconds));
    stop->store(true, std::memory_order_relaxed);
    ioc.stop();

    for (auto& th : threads) {
        th.join();
    }

    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    auto snap = hist->Get();

    double qps = elapsed > 0 ? (static_cast<double>(snap.ok) / elapsed) : 0.0;
    double mbps = elapsed > 0 ? (static_cast<double>(snap.bytes) / elapsed / (1024.0 * 1024.0)) : 0.0;

    auto p50_us = LatencyHistogram::ApproxPercentileUs(snap, 0.50);
    auto p90_us = LatencyHistogram::ApproxPercentileUs(snap, 0.90);
    auto p99_us = LatencyHistogram::ApproxPercentileUs(snap, 0.99);
    auto p999_us = LatencyHistogram::ApproxPercentileUs(snap, 0.999);

    std::cout << "\n=== chmicro_loadgen summary ===\n";
    std::cout << "target: http://" << opt.host << ":" << opt.port << opt.target << "\n";
    std::cout << "threads=" << opt.threads << " concurrency=" << opt.concurrency << " duration=" << opt.duration_seconds << "s\n";
    std::cout << "ok=" << snap.ok << " err=" << snap.err << "\n";
    std::cout << "qps=" << qps << "  recv=" << mbps << " MiB/s\n";
    std::cout << "latency (approx, log2(us) buckets):\n";
    std::cout << "  p50=" << (p50_us / 1000.0) << " ms\n";
    std::cout << "  p90=" << (p90_us / 1000.0) << " ms\n";
    std::cout << "  p99=" << (p99_us / 1000.0) << " ms\n";
    std::cout << "  p999=" << (p999_us / 1000.0) << " ms\n";

    return 0;
}
