#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "memseek/memory_read.hpp"
#include "memseek/scan.hpp"
#include "memseek/value.hpp"
#include "scan_benchmark_helpers.hpp"

namespace {

using benchmark_support::horspoolScan;
using benchmark_support::K_DEFAULT_BUFFER_SIZE;
using benchmark_support::makeAdversarialBuffer;
using benchmark_support::makeRandomBuffer;
using benchmark_support::memmemScan;
using benchmark_support::plantPattern;
using benchmark_support::reportThroughput;
using benchmark_support::stdSearchScan;
using memseek::MemoryRead;
using memseek::MemoryScanner;
using memseek::Value;

void bmScanExactU32Random(benchmark::State& state) {
    const auto buffer = makeRandomBuffer(K_DEFAULT_BUFFER_SIZE, 42);
    MemoryRead memory(0x1000'0000, buffer);
    Value target(static_cast<std::uint32_t>(0xDEADBEEF));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto results = MemoryScanner::scanExact(memory, target);
        benchmark::DoNotOptimize(results);
    }
    reportThroughput(state, K_DEFAULT_BUFFER_SIZE);
}
BENCHMARK(bmScanExactU32Random)->Name("BM_ScanExactU32Random");

void bmScanExactU32Adversarial(benchmark::State& state) {
    const auto buffer =
        makeAdversarialBuffer(K_DEFAULT_BUFFER_SIZE, std::byte{0xEF});
    MemoryRead memory(0x1000'0000, buffer);
    Value target(static_cast<std::uint32_t>(0xDEADBEEF));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto results = MemoryScanner::scanExact(memory, target);
        benchmark::DoNotOptimize(results);
    }
    reportThroughput(state, K_DEFAULT_BUFFER_SIZE);
}
BENCHMARK(bmScanExactU32Adversarial)->Name("BM_ScanExactU32Adversarial");

void bmScanExactU32AllMatches(benchmark::State& state) {
    const auto buffer =
        makeAdversarialBuffer(K_DEFAULT_BUFFER_SIZE, std::byte{0xEF});
    MemoryRead memory(0x1000'0000, buffer);
    Value target(static_cast<std::uint32_t>(0xEFEFEFEF));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto results = MemoryScanner::scanExact(memory, target);
        benchmark::DoNotOptimize(results);
    }
    reportThroughput(state, K_DEFAULT_BUFFER_SIZE);
}
BENCHMARK(bmScanExactU32AllMatches)->Name("BM_ScanExactU32AllMatches");

void bmScanExactString16Random(benchmark::State& state) {
    const auto buffer = makeRandomBuffer(K_DEFAULT_BUFFER_SIZE, 7);
    MemoryRead memory(0x1000'0000, buffer);
    Value target(std::string(16, 'x'));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto results = MemoryScanner::scanExact(memory, target);
        benchmark::DoNotOptimize(results);
    }
    reportThroughput(state, K_DEFAULT_BUFFER_SIZE);
}
BENCHMARK(bmScanExactString16Random)->Name("BM_ScanExactString16Random");

void bmScanExactU32SizeScaling(benchmark::State& state) {
    const std::size_t size =
        static_cast<std::size_t>(state.range(0)) * 1024 * 1024;
    const auto buffer = makeRandomBuffer(size, 99);
    MemoryRead memory(0x1000'0000, buffer);
    Value target(static_cast<std::uint32_t>(0x12345678));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto results = MemoryScanner::scanExact(memory, target);
        benchmark::DoNotOptimize(results);
    }
    reportThroughput(state, size);
}
BENCHMARK(bmScanExactU32SizeScaling)
    ->Name("BM_ScanExactU32SizeScaling")
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Unit(benchmark::kMillisecond);

void bmBaselineMemmemU32Random(benchmark::State& state) {
    const auto buffer = makeRandomBuffer(K_DEFAULT_BUFFER_SIZE, 42);
    MemoryRead memory(0x1000'0000, buffer);
    Value target(static_cast<std::uint32_t>(0xDEADBEEF));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto count = memmemScan(memory, target);
        benchmark::DoNotOptimize(count);
    }
    reportThroughput(state, K_DEFAULT_BUFFER_SIZE);
}
BENCHMARK(bmBaselineMemmemU32Random)->Name("BM_BaselineMemmemU32Random");

void bmBaselineMemmemString16Random(benchmark::State& state) {
    const auto buffer = makeRandomBuffer(K_DEFAULT_BUFFER_SIZE, 7);
    MemoryRead memory(0x1000'0000, buffer);
    Value target(std::string(16, 'x'));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto count = memmemScan(memory, target);
        benchmark::DoNotOptimize(count);
    }
    reportThroughput(state, K_DEFAULT_BUFFER_SIZE);
}
BENCHMARK(bmBaselineMemmemString16Random)
    ->Name("BM_BaselineMemmemString16Random");

void bmBaselineStdSearchU32Random(benchmark::State& state) {
    const auto buffer = makeRandomBuffer(K_DEFAULT_BUFFER_SIZE, 42);
    MemoryRead memory(0x1000'0000, buffer);
    Value target(static_cast<std::uint32_t>(0xDEADBEEF));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto count = stdSearchScan(memory, target);
        benchmark::DoNotOptimize(count);
    }
    reportThroughput(state, K_DEFAULT_BUFFER_SIZE);
}
BENCHMARK(bmBaselineStdSearchU32Random)->Name("BM_BaselineStdSearchU32Random");

void bmBaselineHorspoolU32Random(benchmark::State& state) {
    const auto buffer = makeRandomBuffer(K_DEFAULT_BUFFER_SIZE, 42);
    MemoryRead memory(0x1000'0000, buffer);
    Value target(static_cast<std::uint32_t>(0xDEADBEEF));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto count = horspoolScan(memory, target);
        benchmark::DoNotOptimize(count);
    }
    reportThroughput(state, K_DEFAULT_BUFFER_SIZE);
}
BENCHMARK(bmBaselineHorspoolU32Random)->Name("BM_BaselineHorspoolU32Random");

void bmBaselineHorspoolString16Random(benchmark::State& state) {
    const auto buffer = makeRandomBuffer(K_DEFAULT_BUFFER_SIZE, 7);
    MemoryRead memory(0x1000'0000, buffer);
    Value target(std::string(16, 'x'));
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto count = horspoolScan(memory, target);
        benchmark::DoNotOptimize(count);
    }
    reportThroughput(state, K_DEFAULT_BUFFER_SIZE);
}
BENCHMARK(bmBaselineHorspoolString16Random)
    ->Name("BM_BaselineHorspoolString16Random");

void bmScanExactCorrectnessGuard(benchmark::State& state) {
    for (auto iteration : state) {
        static_cast<void>(iteration);
        state.PauseTiming();
        std::vector<std::byte> buffer = makeRandomBuffer(64 * 1024, 1234);
        const std::array<std::byte, 4> pattern = {
            std::byte{0xAB}, std::byte{0xCD}, std::byte{0x12}, std::byte{0x34}};
        plantPattern(buffer, 32 * 1024, pattern.data(), pattern.size());
        MemoryRead memory(0x4000'0000, buffer);
        Value target(static_cast<std::uint32_t>(0x3412CDAB));
        state.ResumeTiming();

        auto results = MemoryScanner::scanExact(memory, target);
        const bool isCorrect =
            results.size() == 1 &&
            results[0].address() == 0x4000'0000 + (32 * 1024);
        if (!isCorrect) {
            state.SkipWithError("scanExact produced wrong results!");
            break;
        }
        benchmark::DoNotOptimize(results);
    }
}
BENCHMARK(bmScanExactCorrectnessGuard)->Name("BM_ScanExactCorrectnessGuard");

}  // namespace
