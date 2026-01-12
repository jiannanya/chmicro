#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <chmicro/runtime/io_context_pool.h>

namespace chmicro {

class IHttpServer {
public:
    virtual ~IHttpServer() = default;
    virtual void Start() = 0;
    virtual void Stop() = 0;
};

struct AppOptions {
    std::size_t io_threads = 0;
    std::string log_level = "info";
};

class App {
public:
    explicit App(AppOptions options);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    IoContextPool& Io();

    void AddServer(std::shared_ptr<IHttpServer> server);

    // Blocking until Stop() or ctrl-c
    int Run();
    void Stop();

private:
    void SetupLogging();

    AppOptions options_;
    IoContextPool io_;
    std::vector<std::shared_ptr<IHttpServer>> servers_;
};

} // namespace chmicro
