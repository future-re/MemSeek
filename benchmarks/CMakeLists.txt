# Google Benchmark based performance tests for the scanning engine.
# Enabled with -DMEMSEEK_BUILD_BENCHMARKS=ON (requires Release build for
# meaningful numbers).

include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

find_package(benchmark QUIET CONFIG)
if(NOT TARGET benchmark::benchmark)
    FetchContent_Declare(
        benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(benchmark)
endif()

add_executable(scan_benchmark scan_benchmark.cpp)
target_link_libraries(scan_benchmark PRIVATE memseek_lib benchmark::benchmark)
target_compile_options(scan_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O2>)

add_custom_target(run_benchmarks
    COMMAND scan_benchmark --benchmark_filter='BM_Scan|BM_Baseline'
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS scan_benchmark
    USES_TERMINAL
    COMMENT "Running scan performance benchmarks"
)
