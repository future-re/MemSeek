#include <benchmark/benchmark.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include "memseek/memory_read.hpp"
#include "memseek/memory_region.hpp"
#include "memseek/scan.hpp"
#include "memseek/value.hpp"
#include "scan_benchmark_helpers.hpp"

namespace {

using benchmark_support::K_LARGE_REGION_SIZE;
using benchmark_support::peakResidentBytes;
using benchmark_support::plantPattern;
using benchmark_support::reportThroughput;
using memseek::MemoryRead;
using memseek::MemoryScanner;
using memseek::Value;

class LargeRegionFixture {
   public:
    LargeRegionFixture()
        : m_target(static_cast<std::uint32_t>(0x13579BDF)),
          m_storage(K_LARGE_REGION_SIZE) {
        std::mt19937 rng(20260921);
        std::uniform_int_distribution<int> byteDistribution(0, 255);
        const auto forbiddenFirstByte = m_target.data().front();
        for (auto& byte : m_storage) {
            auto randomByte = static_cast<std::byte>(byteDistribution(rng));
            while (randomByte == forbiddenFirstByte) {
                randomByte = static_cast<std::byte>(byteDistribution(rng));
            }
            byte = randomByte;
        }

        constexpr std::array<std::size_t, 3> targetOffsets = {
            64 * 1024 * 1024, 256 * 1024 * 1024, 448 * 1024 * 1024};
        for (const auto offset : targetOffsets) {
            plantPattern(m_storage, offset, m_target.data().data(),
                         m_target.size());
        }
        m_expectedMatches = targetOffsets.size();

        const auto address = reinterpret_cast<std::uintptr_t>(m_storage.data());
        const auto regions = memseek::readProcess(getpid());
        if (!regions) {
            return;
        }

        for (const auto& candidate : regions->getRegions()) {
            if (address < candidate.start ||
                address - candidate.start >= candidate.size) {
                continue;
            }
            m_region = candidate;
            break;
        }
        if (m_region.size == 0) {
            return;
        }

        m_memory = std::make_unique<MemoryRead>(address, std::move(m_storage));
        m_ready = true;
    }

    [[nodiscard]] bool isReady() const noexcept { return m_ready; }
    [[nodiscard]] const MemoryRead& memoryRead() const { return *m_memory; }
    [[nodiscard]] const memseek::MemoryRegion& memoryRegion() const {
        return m_region;
    }
    [[nodiscard]] const Value& scanTarget() const { return m_target; }
    [[nodiscard]] std::size_t expectedMatchCount() const noexcept {
        return m_expectedMatches;
    }

   private:
    Value m_target;
    std::vector<std::byte> m_storage;
    std::unique_ptr<MemoryRead> m_memory;
    memseek::MemoryRegion m_region{};
    std::size_t m_expectedMatches{};
    bool m_ready{false};
};

LargeRegionFixture& largeRegionFixture() {
    static LargeRegionFixture s_fixture;
    return s_fixture;
}

bool fixtureIsReady(benchmark::State& state,
                    const LargeRegionFixture& fixture) {
    if (fixture.isReady()) {
        return true;
    }
    state.SkipWithError("failed to locate the 500 MiB fixture region");
    return false;
}

}  // namespace

static void bmScanExact500MiBInMemory(benchmark::State& state) {
    auto& fixture = largeRegionFixture();
    if (!fixtureIsReady(state, fixture)) {
        return;
    }

    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto results = MemoryScanner::scanExact(fixture.memoryRead(),
                                                fixture.scanTarget());
        if (results.size() != fixture.expectedMatchCount()) {
            state.SkipWithError("in-memory scan returned an unexpected count");
            break;
        }
        benchmark::DoNotOptimize(results);
    }
    reportThroughput(state, fixture.memoryRead().size());
}
BENCHMARK(bmScanExact500MiBInMemory)
    ->Name("BM_ScanExact500MiBInMemory")
    ->Iterations(1);

static void bmRead500MiBProcessRegion(benchmark::State& state) {
    auto& fixture = largeRegionFixture();
    if (!fixtureIsReady(state, fixture)) {
        return;
    }

    std::size_t maxResidentBytes = 0;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        const auto memory =
            MemoryRead::readMemory(getpid(), fixture.memoryRegion());
        if (!memory) {
            state.SkipWithError(memory.error());
            break;
        }
        state.PauseTiming();
        maxResidentBytes = std::max(maxResidentBytes, peakResidentBytes());
        state.ResumeTiming();
        benchmark::DoNotOptimize(memory->data().data());
    }
    state.counters["peak_rss_mib"] =
        static_cast<double>(maxResidentBytes) / (1024.0 * 1024.0);
    reportThroughput(state, fixture.memoryRegion().size);
}
BENCHMARK(bmRead500MiBProcessRegion)
    ->Name("BM_Read500MiBProcessRegion")
    ->Iterations(1);

static void bmScanExact500MiBProcessRegion(benchmark::State& state) {
    auto& fixture = largeRegionFixture();
    if (!fixtureIsReady(state, fixture)) {
        return;
    }

    std::size_t maxResidentBytes = 0;
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto results = MemoryScanner::scanExact(
            getpid(), fixture.memoryRegion(), fixture.scanTarget());
        if (results.size() != fixture.expectedMatchCount()) {
            state.SkipWithError(
                "process-region scan returned an unexpected count");
            break;
        }
        state.PauseTiming();
        maxResidentBytes = std::max(maxResidentBytes, peakResidentBytes());
        state.ResumeTiming();
        benchmark::DoNotOptimize(results);
    }
    state.counters["peak_rss_mib"] =
        static_cast<double>(maxResidentBytes) / (1024.0 * 1024.0);
    reportThroughput(state, fixture.memoryRegion().size);
}
BENCHMARK(bmScanExact500MiBProcessRegion)
    ->Name("BM_ScanExact500MiBProcessRegion")
    ->Iterations(1);
