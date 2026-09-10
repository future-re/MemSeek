#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "memory_region.hpp"
namespace memseek {
class MemoryRead {
   private:
    std::uintptr_t m_address{};
    std::vector<std::byte> m_buffer;

   public:
    MemoryRead(std::uintptr_t address, std::vector<std::byte> buffer)
        : m_address(address), m_buffer(std::move(buffer)) {}

    MemoryRead(std::uintptr_t address, std::size_t size)
        : m_address(address), m_buffer(size) {}

    static std::expected<MemoryRead, std::string> readMemory(
        pid_t pid, const MemoryRegion& region);

    [[nodiscard]]
    std::uintptr_t address() const noexcept {
        return m_address;
    }

    [[nodiscard]]
    std::span<std::byte> data() noexcept {
        return m_buffer;
    }

    [[nodiscard]]
    std::span<const std::byte> data() const noexcept {
        return m_buffer;
    }

    [[nodiscard]]
    std::size_t size() const noexcept {
        return m_buffer.size();
    }
};
}  // namespace memseek