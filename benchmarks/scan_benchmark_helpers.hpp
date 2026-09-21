#pragma once

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "memseek/memory_read.hpp"
#include "memseek/value.hpp"

namespace benchmark_support {

constexpr std::size_t K_DEFAULT_BUFFER_SIZE = 4 * 1024 * 1024;
constexpr std::size_t K_LARGE_REGION_SIZE = 500 * 1024 * 1024;

std::vector<std::byte> makeRandomBuffer(std::size_t size, std::uint32_t seed);
std::vector<std::byte> makeAdversarialBuffer(std::size_t size, std::byte fill);
void plantPattern(std::vector<std::byte>& buffer, std::size_t offset,
                  const std::byte* pattern, std::size_t patternSize);

std::size_t memmemScan(const memseek::MemoryRead& memory,
                       const memseek::Value& target);
std::size_t stdSearchScan(const memseek::MemoryRead& memory,
                          const memseek::Value& target);
std::size_t horspoolScan(const memseek::MemoryRead& memory,
                         const memseek::Value& target);

void reportThroughput(benchmark::State& state, std::size_t bufferSize);
void printBenchmarkLegend();
std::size_t peakResidentBytes();

}  // namespace benchmark_support
