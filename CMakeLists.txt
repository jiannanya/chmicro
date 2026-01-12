cmake_minimum_required(VERSION 3.21)

project(chmicro LANGUAGES CXX)

option(CHMICRO_BUILD_EXAMPLES "Build examples" ON)
option(CHMICRO_BUILD_TESTS "Build tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

# In-tree third-party (header-only) deps
set(CHJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CHJSON_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(CHJSON_BUILD_COMPARE_BENCH OFF CACHE BOOL "" FORCE)
add_subdirectory(another/chjson EXCLUDE_FROM_ALL)

set(CHLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CHLOG_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory(another/chlog EXCLUDE_FROM_ALL)

set(CHTEST_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(another/chtest EXCLUDE_FROM_ALL)

add_library(chmicro)
add_library(chmicro::chmicro ALIAS chmicro)

target_sources(chmicro
  PRIVATE
    src/core/log.cpp
    src/core/status.cpp
    src/core/trace.cpp
    src/core/metrics.cpp
    src/runtime/io_context_pool.cpp
    src/runtime/app.cpp
    src/http/router.cpp
    src/http/http_server.cpp
    src/http/http_client.cpp
    src/governance/service_discovery.cpp
    src/governance/load_balancer.cpp
    src/resilience/retry.cpp
    src/resilience/circuit_breaker.cpp
    src/config/config.cpp
)

target_include_directories(chmicro
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(chmicro
  PUBLIC
    Boost::system
    Threads::Threads
    chlog::chlog
    chjson
)

if(CHMICRO_BUILD_EXAMPLES)
  add_executable(chmicro_hello examples/hello/hello_service.cpp)
  target_link_libraries(chmicro_hello PRIVATE chmicro::chmicro)

  add_executable(chmicro_kv examples/kv/kv_service.cpp)
  target_link_libraries(chmicro_kv PRIVATE chmicro::chmicro)

  add_executable(chmicro_loadgen examples/loadgen/http_loadgen.cpp)
  target_link_libraries(chmicro_loadgen PRIVATE Boost::system)
endif()

if(CHMICRO_BUILD_TESTS)
  enable_testing()

  add_executable(chmicro_tests
    tests/test_main.cpp
    tests/test_router.cpp
    tests/test_circuit_breaker.cpp
    tests/test_trace.cpp
  )
  target_link_libraries(chmicro_tests PRIVATE chmicro::chmicro chtest)
  add_test(NAME chmicro_tests COMMAND chmicro_tests)
endif()
