#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include "memory_region.hpp"

namespace memseek {

struct MemoryChunk {
    std::uintptr_t address{};
    std::size_t size{};
    std::uint64_t parentRegionId{};
};

inline constexpr std::size_t DEFAULT_CHUNK_SIZE = 1024 * 1024;

class MemoryChunkRange {
   public:
    class Iterator {
       public:
        using iterator_concept = std::forward_iterator_tag;

        using iterator_category = std::forward_iterator_tag;

        using value_type = MemoryChunk;
        using difference_type = std::ptrdiff_t;

        Iterator() = default;

        Iterator(const MemoryRegion* region, std::size_t chunkSize,
                 std::uintptr_t current)
            : m_region(region), m_chunkSize(chunkSize), m_current(current) {}

        [[nodiscard]]
        MemoryChunk operator*() const {
            const auto end = m_region->start + m_region->size;

            const auto remaining = end - m_current;

            return {.address = m_current,
                    .size = std::min(m_chunkSize, remaining),
                    .parentRegionId = m_region->id};
        }

        Iterator& operator++() {
            const auto end = m_region->start + m_region->size;

            const auto remaining = end - m_current;

            m_current += std::min(m_chunkSize, remaining);

            return *this;
        }

        Iterator operator++(int) {
            auto old = *this;
            ++(*this);
            return old;
        }

        [[nodiscard]]
        bool operator==(const Iterator& other) const noexcept {
            return m_region == other.m_region && m_current == other.m_current;
        }

        [[nodiscard]]
        bool operator==(std::default_sentinel_t /*unused*/) const noexcept {
            return m_region == nullptr ||
                   m_current >= m_region->start + m_region->size;
        }

       private:
        const MemoryRegion* m_region{};
        std::size_t m_chunkSize{};
        std::uintptr_t m_current{};
    };

    explicit MemoryChunkRange(const MemoryRegion& region,
                              std::size_t chunkSize = DEFAULT_CHUNK_SIZE)
        : m_region(&region), m_chunkSize(chunkSize) {
        if (chunkSize == 0) {
            throw std::invalid_argument("chunkSize must be greater than zero");
        }
    }

    [[nodiscard]]
    Iterator begin() const noexcept {
        return {m_region, m_chunkSize, m_region->start};
    }

    [[nodiscard]]
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

   private:
    const MemoryRegion* m_region{};
    std::size_t m_chunkSize{};
};

static_assert(std::forward_iterator<MemoryChunkRange::Iterator>);

static_assert(std::ranges::forward_range<MemoryChunkRange>);

}  // namespace memseek
