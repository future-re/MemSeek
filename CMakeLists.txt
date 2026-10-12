cmake_minimum_required(VERSION 3.23)

project(memseek VERSION 0.1.0 LANGUAGES CXX)

# Use the package config mode when the installed CMake version supports it.
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# CLI11 is header-only. Keep its target available for the upcoming CLI without
# introducing a dependency into the memory scanning library.
find_package(CLI11 2.7.2 CONFIG QUIET)
if(NOT TARGET CLI11::CLI11)
  include(FetchContent)
  set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    cli11
    URL https://codeload.github.com/CLIUtils/CLI11/tar.gz/refs/tags/v2.7.2
    URL_HASH SHA256=46eef3101da70852ec7af026e09d485ccee81813331c8c6052d39344443b83da
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(cli11)
endif()

include(CodeCoverage)
add_subdirectory(src)

option(MEMSEEK_BUILD_BENCHMARKS "Build Google Benchmark performance tests" ON)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(MEMSEEK_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

setup_coverage_reporting()
