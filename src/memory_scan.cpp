#include "memseek/memory_scan.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <thread>
#include <vector>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>

#include "memseek/memory_reader.hpp"

namespace memseek {

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

    if (targetData.empty()) {
        return results;
    }

    if (memoryData.size() < targetData.size()) {
        return results;
    }

    for (std::size_t offset = 0;
         offset + targetData.size() <= memoryData.size(); ++offset) {
        const auto memoryBegin =
            memoryData.begin() + static_cast<std::ptrdiff_t>(offset);
        if (targetData[0] != memoryBegin[0]) {
            continue;
        }
        if (std::equal(targetData.begin(), targetData.end(), memoryBegin)) {
            results.emplace_back(
                memory.address() + offset,
                std::span<const std::byte>(memoryBegin, targetData.size()));
        }
    }
    return results;
}
std::vector<ScanResult> MemoryScanner::scanRegion(pid_t pid,
                                                  const MemoryRegion& region,
                                                  const Value& target) {
    MemoryReader reader(pid);

    std::vector<MemoryChunk> chunks;
    for (auto chunk : MemoryChunkRange(region)) {
        chunks.push_back(chunk);
    }

    // A single chunk needs no thread pool; run it inline to avoid the cost of
    // spinning up workers for small regions (scanProcess visits many of them).
    if (chunks.size() < 2) {
        std::vector<ScanResult> results;
        for (const auto& chunk : chunks) {
            auto scanRead = reader.read(chunk, region, target);
            if (!scanRead) {
                continue;
            }
            auto chunkResults = scanBuffer(*scanRead, target);
            results.insert(results.end(),
                           std::make_move_iterator(chunkResults.begin()),
                           std::make_move_iterator(chunkResults.end()));
        }
        return results;
    }

    const auto hardwareThreads = std::thread::hardware_concurrency();
    const auto workerCount = std::max<std::size_t>(
        1, std::min<std::size_t>(hardwareThreads == 0 ? 1 : hardwareThreads,
                                 chunks.size()));

    std::vector<std::vector<ScanResult>> chunkResults(chunks.size());

    // The pool is local to this call: its destructor joins every worker, so an
    // exception thrown while posting tasks cannot leave dangling references.
    boost::asio::thread_pool pool(workerCount);

    for (std::size_t index = 0; index < chunks.size(); ++index) {
        boost::asio::post(pool, [&, index] {
            auto scanRead = reader.read(chunks[index], region, target);
            if (scanRead) {
                chunkResults[index] = scanBuffer(*scanRead, target);
            }
        });
    }

    pool.join();

    std::vector<ScanResult> results;
    for (auto& chunkResult : chunkResults) {
        results.insert(results.end(),
                       std::make_move_iterator(chunkResult.begin()),
                       std::make_move_iterator(chunkResult.end()));
    }

    return results;
}
}  // namespace memseek
