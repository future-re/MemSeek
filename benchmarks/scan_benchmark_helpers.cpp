#include "scan_benchmark_helpers.hpp"

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

void reportThroughput(benchmark::State& state, std::size_t bufferSize) {
    state.SetBytesProcessed(static_cast<std::int64_t>(
        state.iterations() * static_cast<std::int64_t>(bufferSize)));
    state.SetLabel("bytes=" + std::to_string(bufferSize));
}

}  // namespace benchmark_support
