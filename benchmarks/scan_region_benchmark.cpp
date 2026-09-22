#include <unistd.h>

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <vector>

#include "memseek/memory_region.hpp"
#include "memseek/memory_scan.hpp"
#include "memseek/value.hpp"
#include "scan_benchmark_helpers.hpp"

namespace {

using memseek::MemoryRegion;
using memseek::MemoryScanner;
using memseek::Value;

constexpr std::uint32_t K_TARGET_VALUE = 0xDEADBEEF;

// Owns a heap buffer of the requested size and locates the process region that
// backs it, so the benchmark measures the real scan path:
// MemoryScanner::scanRegion reading process memory (process_vm_readv +
// chunking) and matching values.
class RegionFixture {
   public:
    explicit RegionFixture(std::size_t size)
        : m_target(static_cast<std::uint32_t>(K_TARGET_VALUE)) {
        m_storage = benchmark_support::makeRandomBuffer(size, 42);

        const std::size_t plantedOffsets[] = {
            8 * 1024 * 1024, size / 2, size - (16 * 1024 * 1024)};
        for (const auto offset : plantedOffsets) {
            std::memcpy(m_storage.data() + offset, &K_TARGET_VALUE,
                        sizeof(K_TARGET_VALUE));
            ++m_plantedMatches;
        }

        const auto address = reinterpret_cast<std::uintptr_t>(m_storage.data());

        auto regions = memseek::readProcess(getpid());
        if (!regions) {
            return;
        }
        for (const auto& candidate : regions->getRegions()) {
            if (address >= candidate.start &&
                address - candidate.start < candidate.size) {
                m_region = candidate;
                break;
            }
        }
        m_ready = m_region.size != 0;
    }

    [[nodiscard]] bool ready() const noexcept { return m_ready; }
    [[nodiscard]] const MemoryRegion& region() const { return m_region; }
    [[nodiscard]] const Value& target() const { return m_target; }
    [[nodiscard]] std::size_t plantedMatches() const noexcept {
        return m_plantedMatches;
    }

   private:
    Value m_target;
    std::vector<std::byte> m_storage;
    MemoryRegion m_region{};
    std::size_t m_plantedMatches{0};
    bool m_ready{false};
};

void runScanRegion(benchmark::State& state, std::size_t size) {
    RegionFixture fixture(size);
    if (!fixture.ready()) {
        state.SkipWithError("failed to locate the fixture process region");
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        try {
            const auto results = MemoryScanner::scanRegion(
                getpid(), fixture.region(), fixture.target());
            if (results.size() < fixture.plantedMatches()) {
                state.SkipWithError("region scan missed a planted match");
                break;
            }
            benchmark::DoNotOptimize(results.data());
        } catch (const std::exception& error) {
            state.SkipWithError(error.what());
            break;
        }
    }

    benchmark_support::reportThroughput(state, fixture.region().size);
}

void scanRegion256MB(benchmark::State& state) {
    runScanRegion(state, 256ull * 1024 * 1024);
}

void scanRegion500MB(benchmark::State& state) {
    runScanRegion(state, 500ull * 1024 * 1024);
}

void scanRegion1GB(benchmark::State& state) {
    runScanRegion(state, 1024ull * 1024 * 1024);
}

}  // namespace

// UseRealTime: the scan runs on worker threads, so the calling thread's CPU
// time is not representative; throughput must be measured against wall time.
BENCHMARK(scanRegion256MB)->Name("BM_ScanRegion256MB")->UseRealTime();
BENCHMARK(scanRegion500MB)->Name("BM_ScanRegion500MB")->UseRealTime();
BENCHMARK(scanRegion1GB)->Name("BM_ScanRegion1GB")->UseRealTime();
