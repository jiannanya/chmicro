#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>

namespace chmicro {

class IoContextPool {
public:
    explicit IoContextPool(std::size_t threads);
    ~IoContextPool();

    IoContextPool(const IoContextPool&) = delete;
    IoContextPool& operator=(const IoContextPool&) = delete;

    // Thread-safe
    boost::asio::io_context& Next();

    void Start();
    void Stop();

private:
    std::size_t threads_{0};
    std::vector<std::unique_ptr<boost::asio::io_context>> contexts_;
    std::vector<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> guards_;
    std::vector<std::thread> workers_;
    std::atomic<std::size_t> rr_{0};
    std::atomic<bool> started_{false};
};

} // namespace chmicro
