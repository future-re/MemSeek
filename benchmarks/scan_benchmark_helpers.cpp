#include "scan_benchmark_helpers.hpp"

#include <cstring>
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

void* randomInsertValue(std::vector<std::byte>& buffer, std::uint32_t value,
                        std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> dist(
        0, buffer.size() - sizeof(value));
    std::size_t index = dist(rng);
    std::memcpy(&buffer[index], &value, sizeof(value));
    return &buffer[index];
}

void reportThroughput(benchmark::State& state, std::size_t bufferSize) {
    state.SetBytesProcessed(static_cast<std::int64_t>(
        state.iterations() * static_cast<std::int64_t>(bufferSize)));
    state.SetLabel("bytes=" + std::to_string(bufferSize));
}

}  // namespace benchmark_support
