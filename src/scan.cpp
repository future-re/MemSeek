#include "memseek/scan.hpp"

namespace memseek {

std::vector<ScanResult> MemoryScanner::scanExact(const MemoryRead& memory,
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
        if(targetData[0]!=memoryBegin[0]) {
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

[[nodiscard]]
std::vector<ScanResult> MemoryScanner::scanExact(pid_t pid,
                                                 MemoryScanLevel level,
                                                 const Value& target) {
    std::vector<ScanResult> results;
    auto memoryResult = readProcess(pid, level);
    if (!memoryResult) {
        return results;
    }
    const auto& memory = *memoryResult;
    for (const auto& region : memory.getRegions()) {
        auto readResult = MemoryRead::readMemory(pid, region);
        if (!readResult) {
            continue;
        }
        const auto& memoryRead = *readResult;
        auto tmpResults = scanExact(memoryRead, target);
        if (!tmpResults.empty()) {
            results.insert(results.end(), tmpResults.begin(), tmpResults.end());
        }
    }
    return results;
}
}  // namespace memseek