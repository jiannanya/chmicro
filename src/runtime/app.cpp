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
    g_app.store(this, std::memory_order_release);
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
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

    // Block until Stop() stops the io contexts.
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Once all contexts are stopped, we consider it finished.
        // (We don't expose a running flag; Stop() will stop contexts.)
        // If one context is still running, Next() could still be usable.
        bool any_running = false;
        // contexts_ not accessible here; infer by attempting post? keep simple.
        // We'll just rely on Stop() breaking by stopping threads; this loop exits when Stop() called.
        // A small shared flag would be cleaner, but keep minimal.
        if (!g_app.load(std::memory_order_acquire)) {
            break;
        }
        (void)any_running;
    }

    return 0;
}

void App::Stop() {
    // idempotent
    if (g_app.exchange(nullptr, std::memory_order_acq_rel) != this) {
        return;
    }

    chmicro::log::info("Stopping app...");
    for (auto& s : servers_) {
        s->Stop();
    }
    io_.Stop();
    chmicro::log::info("Stopped.");
}

} // namespace chmicro
