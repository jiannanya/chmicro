#include <chmicro/runtime/io_context_pool.h>

#include <stdexcept>

namespace chmicro {

IoContextPool::IoContextPool(std::size_t threads) : threads_(threads) {
    if (threads_ == 0) {
        throw std::invalid_argument("IoContextPool threads must be > 0");
    }

    contexts_.reserve(threads_);
    guards_.reserve(threads_);

    for (std::size_t i = 0; i < threads_; ++i) {
        auto ctx = std::make_unique<boost::asio::io_context>(1);
        guards_.push_back(boost::asio::make_work_guard(*ctx));
        contexts_.push_back(std::move(ctx));
    }
}

IoContextPool::~IoContextPool() {
    Stop();
}

boost::asio::io_context& IoContextPool::Next() {
    auto idx = rr_.fetch_add(1, std::memory_order_relaxed) % contexts_.size();
    return *contexts_[idx];
}

void IoContextPool::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }

    workers_.reserve(contexts_.size());
    for (auto& ctx : contexts_) {
        workers_.emplace_back([c = ctx.get()] { c->run(); });
    }
}

void IoContextPool::Stop() {
    bool expected = true;
    if (!started_.compare_exchange_strong(expected, false)) {
        return;
    }

    for (auto& g : guards_) {
        g.reset();
    }
    for (auto& ctx : contexts_) {
        ctx->stop();
    }
    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers_.clear();
}

} // namespace chmicro
