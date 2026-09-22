#include "memseek/memory_scan.hpp"

#include "memseek/memory_reader.hpp"

namespace memseek {

[[nodiscard]]
std::vector<ScanResult> MemoryScanner::scanProcess(pid_t pid,
                                                   MemoryScanLevel level,
                                                   const Value& target) {
    auto regionList = *readProcess(pid, level);
    if (!regionList.empty()) {
        return {};
    }

    std::vector<ScanResult> results;
    for (const auto& region : regionList.getRegions()) {
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
    auto chunkRegion = MemoryChunkRange(region);
    std::vector<ScanResult> results;
    for (auto chunk : chunkRegion) {
        auto scanRead = reader.read(chunk, target);
        if (!scanRead) {
            continue;
        }
        auto tmpResults = scanBuffer(*scanRead, target);
        if (!tmpResults.empty()) {
            results.insert(results.end(), tmpResults.begin(), tmpResults.end());
        }
    }
    return results;
}
}  // namespace memseek
