#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // for memmem()
#endif
#include "scan_benchmark_helpers.hpp"

#include <string.h>  // NOLINT(modernize-deprecated-headers)
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <random>
#include <string>

namespace benchmark_support {

std::vector<std::byte> makeRandomBuffer(std::size_t size, std::uint32_t seed) {
    std::vector<std::byte> buffer(size);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& byte : buffer) {
        byte = static_cast<std::byte>(dist(rng));
    }
    return buffer;
}

std::vector<std::byte> makeAdversarialBuffer(std::size_t size, std::byte fill) {
    return std::vector<std::byte>(size, fill);
}

void plantPattern(std::vector<std::byte>& buffer, std::size_t offset,
                  const std::byte* pattern, std::size_t patternSize) {
    std::memcpy(buffer.data() + offset, pattern, patternSize);
}

std::size_t memmemScan(const memseek::MemoryRead& memory,
                       const memseek::Value& target) {
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

std::size_t stdSearchScan(const memseek::MemoryRead& memory,
                          const memseek::Value& target) {
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

std::size_t horspoolScan(const memseek::MemoryRead& memory,
                         const memseek::Value& target) {
    const auto memoryData = memory.data();
    const auto targetData = target.data();
    const std::size_t memorySize = memoryData.size();
    const std::size_t targetDataSize = targetData.size();
    if (targetDataSize == 0 || memorySize < targetDataSize) {
        return 0;
    }

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

std::size_t peakResidentBytes() {
    rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
    return static_cast<std::size_t>(usage.ru_maxrss) * 1024;
}

}  // namespace benchmark_support
