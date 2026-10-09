#include "memseek/memory/change.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>

#include "memseek/memory/chunk.hpp"
#include "memseek/memory/reader.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/memory/snapshot.hpp"

namespace memseek {

auto matchesChange(const std::span<const std::byte> previous,
                   const std::span<const std::byte> current,
                   const ChangeType type) -> bool {
    if (previous.empty() || previous.size() != current.size()) {
        return false;
    }
    const bool equal = std::ranges::equal(previous, current);
    switch (type) {
        case ChangeType::CHANGED:
            return !equal;
        case ChangeType::UNCHANGED:
            return equal;
    }
    return false;
}

auto captureChanges(const MemorySnapshot& previous, const ChangeType type)
    -> std::expected<MemorySnapshot, std::string> {
    if (type != ChangeType::CHANGED && type != ChangeType::UNCHANGED) {
        return std::unexpected("invalid change type");
    }
    // Follow the saved addresses, even if their mappings are now read-only.
    MemoryScanLevel level;
    level.memoryProtection = static_cast<std::uint8_t>(MemoryProtection::READ);
    auto regions = readProcess(previous.pid(), level);
    if (!regions) {
        return std::unexpected(regions.error());
    }

    const MemoryReader reader(previous.pid());
    MemorySnapshot::Results results;
    for (const auto& candidate : previous.results()) {
        const auto width = candidate.data().size();
        if (width == 0) {
            continue;
        }
        const auto region = std::ranges::find_if(
            regions->getRegions(),
            [&candidate, width](const MemoryRegion& item) {
                return candidate.address() >= item.start &&
                       candidate.address() - item.start < item.size &&
                       width <= item.size - (candidate.address() - item.start);
            });
        if (region == regions->getRegions().end()) {
            continue;
        }
        const MemoryChunk chunk{
            .address = candidate.address(),
            .size = width,
            .parentRegionId = region->id,
        };
        const auto current = reader.read(chunk, *region, 0);
        if (current && matchesChange(candidate.data(), current->data(), type)) {
            results.emplace_back(candidate.address(), current->data());
        }
    }
    return MemorySnapshot::fromScanResults(previous.pid(), std::move(results));
}

}  // namespace memseek
