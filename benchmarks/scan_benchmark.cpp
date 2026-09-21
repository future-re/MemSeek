// Benchmark entry point. Individual benchmark groups live in:
//   - scan_exact_benchmark.cpp: in-memory scanner and algorithm baselines
//   - large_region_benchmark.cpp: 500 MiB process-region scenarios
//   - scan_benchmark_helpers.cpp: shared data and reporting helpers

#include <benchmark/benchmark.h>

#include "scan_benchmark_helpers.hpp"

int main(int argc, char** argv) {
    benchmark::MaybeReenterWithoutASLR(argc, argv);
    benchmark_support::printBenchmarkLegend();

    benchmark::Initialize(&argc, argv);
    benchmark::AddCustomContext("implementation", "MemoryScanner::scanExact");
    benchmark::AddCustomContext(
        "baselines", "glibc memmem; std::search; Boyer-Moore-Horspool");
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
