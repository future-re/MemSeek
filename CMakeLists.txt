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
