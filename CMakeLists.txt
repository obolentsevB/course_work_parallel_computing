cmake_minimum_required(VERSION 3.20)
project(parallel_search LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Single-header dependencies: cpp-httplib (HTTP + WebSocket) and nlohmann/json.
set(DEPS_DIR ${CMAKE_BINARY_DIR}/_deps/include)
function(fetch_header url dest)
  if(NOT EXISTS ${dest})
    file(DOWNLOAD ${url} ${dest} STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
      file(REMOVE ${dest})
      message(FATAL_ERROR "Failed to download ${url}: ${status}")
    endif()
  endif()
endfunction()
fetch_header(https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.64.0/httplib.h
             ${DEPS_DIR}/httplib.h)
fetch_header(https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp
             ${DEPS_DIR}/nlohmann/json.hpp)

find_package(Threads REQUIRED)

add_library(search_core STATIC
  src/concurrency/thread_pool.cpp
  src/index/inverted_index.cpp
  src/api/http_server.cpp
)
target_include_directories(search_core PUBLIC src ${DEPS_DIR})
target_link_libraries(search_core PUBLIC Threads::Threads)
target_compile_options(search_core PUBLIC -Wall -Wextra)

add_executable(server src/main.cpp)
target_link_libraries(server PRIVATE search_core)