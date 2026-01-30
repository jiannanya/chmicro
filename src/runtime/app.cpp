#include <chmicro/runtime/app.h>

#include <chmicro/core/log.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <boost/asio/signal_set.hpp>
#endif

namespace chmicro {
namespace {
std::atomic<App*> g_app{nullptr};

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType);
#endif

struct ScopedAppRegistration {
    explicit ScopedAppRegistration(App* app) : app_(app) {
        App* expected = nullptr;
        if (!g_app.compare_exchange_strong(expected, app_, std::memory_order_acq_rel)) {
            // Another App is already registered. Keep the existing one and continue.
            // This framework currently expects a single active App.
            app_ = nullptr;
            return;
        }

#ifdef _WIN32
        if (SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
            handler_installed_ = true;
        }
#endif
    }

    ~ScopedAppRegistration() {
#ifdef _WIN32
        if (handler_installed_) {
            SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
        }
#endif

        if (app_ != nullptr) {
            App* expected = app_;
            (void)g_app.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        }
    }

    ScopedAppRegistration(const ScopedAppRegistration&) = delete;
    ScopedAppRegistration& operator=(const ScopedAppRegistration&) = delete;

private:
    App* app_{nullptr};
    bool handler_installed_{false};
};

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        if (auto* app = g_app.load(std::memory_order_acquire)) {
            app->Stop();
            return TRUE;
        }
    }
    return FALSE;
}
#endif

} // namespace

App::App(AppOptions options)
    : options_(std::move(options)),
      io_([&] {
          auto n = options_.io_threads;
          if (n != 0) {
              return n;
          }
          auto hc = static_cast<std::size_t>(std::thread::hardware_concurrency());
          return hc == 0 ? static_cast<std::size_t>(1) : hc;
      }()) {
    if (io_.Next().stopped()) {
        // no-op: silence -Wmaybe-uninitialized in some compilers
    }
    SetupLogging();
}

App::~App() {
    Stop();
}

void App::SetupLogging() {
    chmicro::log::Init(options_.log_level);
}

IoContextPool& App::Io() {
    return io_;
}

void App::AddServer(std::shared_ptr<IHttpServer> server) {
    servers_.push_back(std::move(server));
}

int App::Run() {
    ScopedAppRegistration reg(this);

    {
        std::lock_guard<std::mutex> lk(stop_mu_);
        stopped_ = false;
    }
    stop_requested_.store(false, std::memory_order_release);

#ifndef _WIN32
    // POSIX: stop the app on Ctrl+C / SIGTERM.
    // Keep the signal_set alive by capturing it.
    auto signals = std::make_shared<boost::asio::signal_set>(io_.Next(), SIGINT, SIGTERM);
    signals->async_wait([this, signals](const boost::system::error_code&, int) {
        this->Stop();
    });
#endif

    io_.Start();

    for (auto& s : servers_) {
        s->Start();
    }

    // Block until Stop() completes.
    {
        std::unique_lock<std::mutex> lk(stop_mu_);
        stop_cv_.wait(lk, [&] { return stopped_; });
    }

    return 0;
}

void App::Stop() {
    // idempotent
    if (stop_requested_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    g_app.store(nullptr, std::memory_order_release);

    chmicro::log::info("Stopping app...");
    for (auto& s : servers_) {
        s->Stop();
    }
    io_.Stop();
    chmicro::log::info("Stopped.");

    {
        std::lock_guard<std::mutex> lk(stop_mu_);
        stopped_ = true;
    }
    stop_cv_.notify_all();
}

} // namespace chmicro
