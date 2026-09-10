// Performance benchmarks for the memory scanning engine.
//
// Build & run:
//   cmake -B build-bench -DMEMSEEK_BUILD_BENCHMARKS=ON
//   -DCMAKE_BUILD_TYPE=Release cmake --build build-bench --target
//   run_benchmarks
//
// Comparing two implementations (to prove a performance improvement):
//   ./build/benchmarks/scan_benchmark --benchmark_out=before.json \
//       --benchmark_out_format=json --benchmark_filter='BM_ScanExact'
//   # ...apply your optimization to src/scan.cpp, rebuild...
//   ./build/benchmarks/scan_benchmark --benchmark_out=after.json \
//       --benchmark_out_format=json --benchmark_filter='BM_ScanExact'
//   # then compare:
//   python3 benchmarks/compare.py before.json after.json

#include <benchmark/benchmark.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // for memmem()
#endif
#include <string.h>  // NOLINT(modernize-deprecated-headers)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "memseek/memory_read.hpp"
#include "memseek/scan.hpp"
#include "memseek/value.hpp"

namespace {

using memseek::MemoryRead;
using memseek::MemoryScanner;
using memseek::Value;

// ---------------------------------------------------------------------------
// Fixture data generators
// ---------------------------------------------------------------------------

// Buffer filled with pseudo-random bytes: realistic case, very few matches.
std::vector<std::byte> makeRandomBuffer(std::size_t size, std::uint32_t seed) {
    std::vector<std::byte> buffer(size);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& byte : buffer) {
        byte = static_cast<std::byte>(dist(rng));
    }
    return buffer;
}

// Worst case for a first-byte pre-filter: every byte matches the pattern's
// first byte, so the full comparison runs at (almost) every offset.
std::vector<std::byte> makeAdversarialBuffer(std::size_t size, std::byte fill) {
    return std::vector<std::byte>(size, fill);
}

// Place a known pattern at a fixed position inside a random buffer.
void plantPattern(std::vector<std::byte>& buffer, std::size_t offset,
                  const std::byte* pattern, std::size_t patternSize) {
    std::memcpy(buffer.data() + offset, pattern, patternSize);
}

// ---------------------------------------------------------------------------
// Mature tool baseline: glibc memmem (Two-Way algorithm)
// ---------------------------------------------------------------------------
// 参照不是自己写的简化代码,而是系统里经过高度优化的成熟扫描工具,
// 用来衡量自己的实现与业界水平的差距。

std::size_t memmemScan(const MemoryRead& memory, const Value& target) {
    const auto memoryData = memory.data();
    const auto targetData = target.data();
    if (targetData.empty() || memoryData.size() < targetData.size()) {
        return 0;
    }
    std::size_t count = 0;
    const auto* haystack = reinterpret_cast<const char*>(memoryData.data());
    const auto* needle = reinterpret_cast<const char*>(targetData.data());
    const char* cursor = haystack;
    std::size_t remaining = memoryData.size();
    while (remaining >= targetData.size()) {
        const void* hit = memmem(cursor, remaining, needle, targetData.size());
        if (hit == nullptr) {
            break;
        }
        ++count;
        const auto advance = static_cast<const char*>(hit) - cursor + 1;
        cursor += advance;
        remaining -= static_cast<std::size_t>(advance);
    }
    return count;
}

// ---------------------------------------------------------------------------
// Industry-standard algorithm baselines
// ---------------------------------------------------------------------------
// 参照不是临时写的简化代码,而是行业标准实现/经典算法,用来衡量自己的
// 实现与业界水平的差距:
//   - glibc memmem   : 系统高度优化的 Two-Way 算法
//   - std::search    : C++ 标准库通用搜索
//   - Boyer-Moore-Horspool : 经典教材算法 (跳过式启发)

// C++ 标准库 std::search(通用线性搜索)
std::size_t stdSearchScan(const MemoryRead& memory, const Value& target) {
    const auto memoryData = memory.data();
    const auto targetData = target.data();
    if (targetData.empty() || memoryData.size() < targetData.size()) {
        return 0;
    }
    std::size_t count = 0;
    auto cursor = memoryData.begin();
    const auto end = memoryData.end();
    while (static_cast<std::size_t>(end - cursor) >= targetData.size()) {
        const auto hit =
            std::search(cursor, end, targetData.begin(), targetData.end());
        if (hit == end) {
            break;
        }
        ++count;
        cursor = hit + 1;
    }
    return count;
}

// Boyer-Moore-Horspool 经典算法(坏字符启发式跳跃)
std::size_t horspoolScan(const MemoryRead& memory, const Value& target) {
    const auto memoryData = memory.data();
    const auto targetData = target.data();
    const std::size_t memorySize = memoryData.size();
    const std::size_t targetDataSize = targetData.size();
    if (targetDataSize == 0 || memorySize < targetDataSize) {
        return 0;
    }
    // 坏字符表: 256 项,记录模式中每个字节最后出现的位置
    std::array<std::size_t, 256> shift{};
    shift.fill(targetDataSize);
    for (std::size_t i = 0; i + 1 < targetDataSize; ++i) {
        shift[static_cast<unsigned char>(
            std::to_integer<unsigned>(targetData[i]))] = targetDataSize - 1 - i;
    }

    std::size_t count = 0;
    std::size_t pos = 0;
    while (pos + targetDataSize <= memorySize) {
        std::size_t targetIndex = targetDataSize - 1;
        while (memoryData[pos + targetIndex] == targetData[targetIndex]) {
            if (targetIndex == 0) {
                ++count;
                break;
            }
            --targetIndex;
        }
        pos += shift[static_cast<unsigned char>(
            std::to_integer<unsigned>(memoryData[pos + targetDataSize - 1]))];
    }
    return count;
}

// ---------------------------------------------------------------------------
// Benchmark helpers
// ---------------------------------------------------------------------------

void reportThroughput(benchmark::State& state, std::size_t bufferSize) {
    state.SetBytesProcessed(static_cast<std::int64_t>(
        state.iterations() * static_cast<std::int64_t>(bufferSize)));
    state.SetLabel("bytes=" + std::to_string(bufferSize));
}

void printBenchmarkLegend() {
    std::cerr << "\n"
              << "========== Benchmark legend ==========\n"
              << "[YOUR IMPLEMENTATION / 自研实现]\n"
              << "  BM_ScanExact* -> MemoryScanner::scanExact\n"
              << "[BASELINES / 对照实现]\n"
              << "  BM_Baseline* -> glibc memmem / std::search / "
                 "Boyer-Moore-Horspool\n"
              << "======================================\n\n";
}

constexpr std::size_t K_DEFAULT_BUFFER_SIZE = 4 * 1024 * 1024;  // 4 MiB

}  // namespace

// ---------------------------------------------------------------------------
// Current implementation: MemoryScanner::scanExact
// ---------------------------------------------------------------------------

// Realistic case: random 4 MiB buffer, scan for a uint32 value.
static void bmScanExactU32Random(benchmark::State& state) {
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

// Worst case: buffer is filled with the pattern's first byte, so the
// first-byte pre-filter never helps and every offset does a full compare.
static void bmScanExactU32Adversarial(benchmark::State& state) {
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

// Many matches: buffer is entirely the repeated 4-byte pattern.
static void bmScanExactU32AllMatches(benchmark::State& state) {
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

// Longer pattern (16-byte string): measures how the scan scales with
// pattern length.
static void bmScanExactString16Random(benchmark::State& state) {
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

// Scaling with buffer size: 1 MiB / 4 MiB / 16 MiB.
static void bmScanExactU32SizeScaling(benchmark::State& state) {
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

// ---------------------------------------------------------------------------
// Mature tool baselines: our implementation vs glibc memmem
// ---------------------------------------------------------------------------

static void bmBaselineMemmemU32Random(benchmark::State& state) {
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

static void bmBaselineMemmemString16Random(benchmark::State& state) {
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

// C++ 标准库 std::search 基线
static void bmBaselineStdSearchU32Random(benchmark::State& state) {
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

// Boyer-Moore-Horspool 经典算法基线
static void bmBaselineHorspoolU32Random(benchmark::State& state) {
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

static void bmBaselineHorspoolString16Random(benchmark::State& state) {
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

// ---------------------------------------------------------------------------
// Correctness guard: benchmarks are useless if the scan is wrong.
// ---------------------------------------------------------------------------

static void bmScanExactCorrectnessGuard(benchmark::State& state) {
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
        bool isCorrect = results.size() == 1 &&
                         results[0].address() == 0x4000'0000 + (32 * 1024);
        if (!isCorrect) {
            state.SkipWithError("scanExact produced wrong results!");
            break;
        }
        benchmark::DoNotOptimize(results);
    }
}
BENCHMARK(bmScanExactCorrectnessGuard)->Name("BM_ScanExactCorrectnessGuard");

int main(int argc, char** argv) {
    benchmark::MaybeReenterWithoutASLR(argc, argv);
    printBenchmarkLegend();

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
