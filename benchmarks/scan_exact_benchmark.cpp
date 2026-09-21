#include <benchmark/benchmark.h>

#include <cstdint>
#include <utility>

#include "memseek/memory_read.hpp"
#include "memseek/scan.hpp"
#include "memseek/value.hpp"
#include "scan_benchmark_helpers.hpp"

namespace {

void scanRandomBuffer(benchmark::State& state) {
    const auto size = benchmark_support::K_BUFFER_SIZE;
    auto buffer = benchmark_support::makeRandomBuffer(size, 42);
    memseek::MemoryRead memory(0x1000'0000, std::move(buffer));
    const memseek::Value target(static_cast<std::uint32_t>(0xDEADBEEF));

    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto results = memseek::MemoryScanner::scanExact(memory, target);
        benchmark::DoNotOptimize(results.data());
    }

    benchmark_support::reportThroughput(state, size);
}

void scanRandomBuffer500MB(benchmark::State& state) {
    const auto size = 500 * 1024 * 1024;
    auto buffer = benchmark_support::makeRandomBuffer(size, 42);
    memseek::MemoryRead memory(0x1000'0000, std::move(buffer));
    const memseek::Value target(static_cast<std::uint32_t>(0xDEADBEEF));
    auto* ptr =
        benchmark_support::randomInsertValue(memory.buffer(), 0xDEADBEEF, 42);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto results = memseek::MemoryScanner::scanExact(memory, target);
        benchmark::DoNotOptimize(results.data());
    }

    benchmark_support::reportThroughput(state, size);
}

void scanRandomBuffer1GB(benchmark::State& state) {
    const auto size = 1024 * 1024 * 1024;
    auto buffer = benchmark_support::makeRandomBuffer(size, 42);
    memseek::MemoryRead memory(0x1000'0000, std::move(buffer));
    const memseek::Value target(static_cast<std::uint32_t>(0xDEADBEEF));
    for (int i = 0; i < 10; i++) {
        auto* ptr = benchmark_support::randomInsertValue(memory.buffer(),
                                                         0xDEADBEEF, 42 + i);
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto results = memseek::MemoryScanner::scanExact(memory, target);
        benchmark::DoNotOptimize(results.data());
    }

    benchmark_support::reportThroughput(state, size);
}

}  // namespace

// Register the benchmark
BENCHMARK(scanRandomBuffer)->Name("BM_ScanExactRandom");
BENCHMARK(scanRandomBuffer500MB)->Name("BM_ScanExactRandom500MB");
BENCHMARK(scanRandomBuffer1GB)->Name("BM_ScanExactRandom1GB");
