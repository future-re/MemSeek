#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace memseek {

class MemoryRead {
   public:
    MemoryRead(std::uintptr_t address, std::size_t logicalSize,
               std::vector<std::byte> buffer,
               std::uint64_t parentRegionId = 0)
        : m_address(address),
          m_logicalSize(logicalSize),
          m_parentRegionId(parentRegionId),
          m_buffer(std::move(buffer)) {}

    MemoryRead(std::uintptr_t address, std::size_t logicalSize,
               std::uint64_t parentRegionId = 0)
        : m_address(address),
          m_logicalSize(logicalSize),
          m_parentRegionId(parentRegionId),
          m_buffer(logicalSize) {}

    [[nodiscard]]
    std::uintptr_t address() const noexcept {
        return m_address;
    }

    [[nodiscard]]
    std::size_t size() const noexcept {
        return m_buffer.size();
    }

    [[nodiscard]]
    std::size_t logicalSize() const noexcept {
        return m_logicalSize;
    }

    [[nodiscard]]
    std::uint64_t parentRegionId() const noexcept {
        return m_parentRegionId;
    }

    [[nodiscard]]
    std::uintptr_t logicalEnd() const noexcept {
        return m_address + m_logicalSize;
    }

    [[nodiscard]]
    std::span<const std::byte> data() const noexcept {
        return m_buffer;
    }

    [[nodiscard]]
    std::vector<std::byte>& buffer() noexcept {
        return m_buffer;
    }

   private:
    std::uintptr_t m_address{};
    std::size_t m_logicalSize{};
    std::uint64_t m_parentRegionId{};
    std::vector<std::byte> m_buffer;
};

}  // namespace memseek