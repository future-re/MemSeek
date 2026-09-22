#pragma once

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace benchmark_support {

std::vector<std::byte> makeRandomBuffer(std::size_t size, std::uint32_t seed);

void reportThroughput(benchmark::State& state, std::size_t bufferSize);

}  // namespace benchmark_support
