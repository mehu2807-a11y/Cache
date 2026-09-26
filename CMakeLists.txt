cmake_minimum_required(VERSION 3.10)
project(CacheBench CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(Threads REQUIRED)

include_directories(include)

add_executable(cachebench_server
    src/main.cpp
    src/thread_pool.cpp
    src/tcp_server.cpp
)
target_link_libraries(cachebench_server PRIVATE Threads::Threads)

add_executable(bench_client
    bench/bench_client.cpp
)
target_link_libraries(bench_client PRIVATE Threads::Threads)
