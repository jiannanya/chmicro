# chmicro

A lightweight high-performance C++20 microservice framework (HTTP/1.1 + async runtime) built with **CMake + Ninja + Clang** and **vcpkg**.

## Prereqs (Windows)

- Visual Studio Build Tools (MSVC linker + Windows SDK)
- LLVM (clang-cl)
- Ninja
- CMake >= 3.21
- vcpkg (set env var `VCPKG_ROOT` to the vcpkg root directory)

## Prereqs (Linux/macOS)

- clang/clang++
- Ninja
- CMake >= 3.21
- vcpkg (export env var `VCPKG_ROOT` to the vcpkg root directory)

## Build

```powershell
# From repo root
cmake --preset clangcl-ninja-debug
cmake --build --preset build-debug
ctest --preset test-debug
```

## Build (Linux/macOS)

```sh
cmake --preset clang-ninja-debug
cmake --build --preset build-posix-debug
ctest --preset test-posix-debug
```

## Run example

```powershell
.\out\build\clangcl-ninja-debug\chmicro_hello.exe --listen 0.0.0.0:8086
# then:
# curl http://127.0.0.1:8086/health
# curl http://127.0.0.1:8086/hello?name=world
# curl http://127.0.0.1:8086/metrics
```

## High-load demo: KV service + async loadgen

### 1) Run KV service

```powershell
.\out\build\clangcl-ninja-release\chmicro_kv.exe --listen 0.0.0.0:8087 --threads 8 --shards 128
# endpoints:
#   GET  /health
#   GET  /get?key=foo
#   POST /put  {"key":"foo","value":"bar"}
#   GET  /compute?iters=100000
#   GET  /metrics
```

### 2) Warm up data (optional)

```powershell
# PowerShell note: `curl` may be an alias to Invoke-WebRequest.
# Prefer curl.exe explicitly, or use Invoke-RestMethod.

# Option A: curl.exe
curl.exe -X POST http://127.0.0.1:8087/put -H "Content-Type: application/json" -d "{\"key\":\"hot\",\"value\":\"v\"}"

# Option B: Invoke-RestMethod
Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:8087/put" -ContentType "application/json" -Body '{"key":"hot","value":"v"}'
```

### 3) Run loadgen

```powershell
.\out\build\clangcl-ninja-release\chmicro_loadgen.exe --host 127.0.0.1 --port 8087 --target "/get?key=hot" --threads 4 --concurrency 256 --warmup 2 --duration 10 --timeout-ms 1000
```

### Observe

- Server-side metrics: `curl http://127.0.0.1:8087/metrics`
- Tune: increase `--threads` (server), `--concurrency` (client), and use Release build for throughput.
