#include "memseek/memory_scan.hpp"

#include <algorithm>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <vector>

#include "memseek/memory_reader.hpp"

namespace memseek {

MemoryScanner::MemoryScanner(std::size_t workerCount)
    : m_pool(std::max<std::size_t>(1, workerCount == 0 ? 1 : workerCount)) {}

MemoryScanner::~MemoryScanner() { m_pool.join(); }

[[nodiscard]]
std::vector<ScanResult> MemoryScanner::scanProcess(pid_t pid,
                                                   MemoryScanLevel level,
                                                   const Value& target) {
    auto regionList = readProcess(pid, level);
    if (!regionList) {
        return {};
    }

    std::vector<ScanResult> results;
    for (const auto& region : regionList->getRegions()) {
        auto regionResults = scanRegion(pid, region, target);
        if (!regionResults.empty()) {
            results.insert(results.end(), regionResults.begin(),
                           regionResults.end());
        }
    }
    return results;
}

std::vector<ScanResult> MemoryScanner::scanBuffer(const MemoryRead& memory,
                                                  const Value& target) {
    std::vector<ScanResult> results;

    const auto memoryData = memory.data();
    const auto targetData = target.data();

    const std::size_t memorySize = memoryData.size();
    const std::size_t targetSize = targetData.size();

    if (targetSize == 0 || memorySize < targetSize) {
        return results;
    }

    const auto* memoryPtr = memoryData.data();
    const auto* targetPtr = targetData.data();

    const std::size_t lastOffset = memorySize - targetSize;

    for (std::size_t offset = 0; offset <= lastOffset; ++offset) {
        const auto* current = memoryPtr + offset;

        if (*current != *targetPtr) {
            continue;
        }

        if (std::equal(targetPtr + 1, targetPtr + targetSize, current + 1)) {
            results.emplace_back(
                memory.address() + offset,
                std::span<const std::byte>(current, targetSize));
        }
    }

    return results;
}

std::vector<ScanResult> MemoryScanner::scanRegion(pid_t pid,
                                                  const MemoryRegion& region,
                                                  const Value& target) {
    if (region.size == 0) {
        return {};
    }

    MemoryReader reader(pid);

    const std::size_t chunkCount =
        (region.size + DEFAULT_CHUNK_SIZE - 1) / DEFAULT_CHUNK_SIZE;

    //
    // Small region: avoid thread-pool scheduling overhead.
    //
    if (chunkCount == 1) {
        const auto range = MemoryChunkRange(region);
        const auto chunk = *range.begin();

        auto memory = reader.read(chunk, region, target);

        if (!memory) {
            return {};
        }

        return scanBuffer(*memory, target);
    }

    //
    // One future per chunk.
    // The chunks themselves are still generated lazily.
    //
    std::vector<std::future<std::vector<ScanResult>>> futures;

    futures.reserve(chunkCount);

    for (const auto chunk : MemoryChunkRange(region)) {
        futures.emplace_back(submit([&, chunk]() -> std::vector<ScanResult> {
            auto memory = reader.read(chunk, region, target);

            if (!memory) {
                return {};
            }

            auto results = scanBuffer(*memory, target);

            //
            // Reader may read overlap bytes so that
            // matches spanning chunk boundaries are
            // detectable.
            //
            // A chunk only owns matches whose starting
            // address lies inside its logical range.
            //
            const auto logicalEnd = chunk.address + chunk.size;

            std::erase_if(results, [logicalEnd](const ScanResult& result) {
                return result.address() >= logicalEnd;
            });

            return results;
        }));
    }

    //
    // Collect per-chunk results.
    //
    std::vector<std::vector<ScanResult>> chunkResults;

    chunkResults.reserve(futures.size());

    std::size_t totalMatches = 0;

    for (auto& future : futures) {
        auto result = future.get();

        totalMatches += result.size();

        chunkResults.emplace_back(std::move(result));
    }

    //
    // Merge once after all workers have finished.
    //
    std::vector<ScanResult> results;
    results.reserve(totalMatches);

    for (auto& chunkResult : chunkResults) {
        results.insert(results.end(),
                       std::make_move_iterator(chunkResult.begin()),
                       std::make_move_iterator(chunkResult.end()));
    }

    return results;
}
}  // namespace memseek
