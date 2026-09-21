#pragma once

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace benchmark_support {

constexpr std::size_t K_BUFFER_SIZE = 4 * 1024 * 1024;

std::vector<std::byte> makeRandomBuffer(std::size_t size, std::uint32_t seed);

void* randomInsertValue(std::vector<std::byte>& buffer, std::uint32_t value,
                        std::uint32_t seed);

void reportThroughput(benchmark::State& state, std::size_t bufferSize);

}  // namespace benchmark_support
