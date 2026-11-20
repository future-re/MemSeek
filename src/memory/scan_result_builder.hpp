#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include "memseek/memory/scan.hpp"

namespace memseek {

// Internal append-only blocks keep saved spans stable while results grow or
// move. Small initial blocks limit waste for sparse scans; dense scans amortize
// allocations over blocks of up to 64 KiB. Results keep their block alive.
class ScanResultBuilder {
   public:
    ScanResultBuilder() = default;
    ~ScanResultBuilder() = default;
    ScanResultBuilder(const ScanResultBuilder&) = delete;
    ScanResultBuilder& operator=(const ScanResultBuilder&) = delete;
    ScanResultBuilder(ScanResultBuilder&&) = delete;
    ScanResultBuilder& operator=(ScanResultBuilder&&) = delete;

    auto make(std::uintptr_t address, std::span<const std::byte> data,
              std::uint64_t parentRegionId = 0) -> ScanResult {
        if (data.empty()) {
            return ScanResult(address, {}, parentRegionId);
        }
        if (data.size() > m_capacity - m_used) {
            const auto capacity = std::max(data.size(), m_nextCapacity);
            // NOLINTNEXTLINE(modernize-avoid-c-arrays)
            m_block = std::make_shared_for_overwrite<std::byte[]>(capacity);
            m_capacity = capacity;
            m_used = 0;
            m_nextCapacity = std::min(m_nextCapacity * 2, MAX_BLOCK_SIZE);
        }
        auto* destination = m_block.get() + m_used;
        std::memcpy(destination, data.data(), data.size());
        m_used += data.size();
        return {address,
                std::shared_ptr<const std::byte[]>(m_block, destination),
                data.size(), parentRegionId};
    }

    static auto atAddress(const ScanResult& saved, std::uintptr_t address,
                          std::uint64_t parentRegionId) -> ScanResult {
        return {address, saved.m_data, saved.data().size(), parentRegionId};
    }

   private:
    static constexpr std::size_t MAX_BLOCK_SIZE = 64 * 1024;
    std::shared_ptr<std::byte[]> m_block;  // NOLINT(modernize-avoid-c-arrays)
    std::size_t m_used{};
    std::size_t m_capacity{};
    std::size_t m_nextCapacity{64};
};

}  // namespace memseek
