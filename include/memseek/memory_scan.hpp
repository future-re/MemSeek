#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "memseek/memory_read.hpp"
#include "memseek/memory_region.hpp"
#include "memseek/value.hpp"
namespace memseek {

class ScanResult {
   public:
    explicit ScanResult(std::uintptr_t address = 0,
                        std::span<const std::byte> data = {})
        : m_address(address), m_data(data.begin(), data.end()) {}

    [[nodiscard]]
    std::uintptr_t address() const noexcept {
        return m_address;
    }

    [[nodiscard]]
    std::span<const std::byte> data() const noexcept {
        return m_data;
    }

    bool operator==(const ScanResult&) const = default;

   private:
    std::uintptr_t m_address{};
    std::vector<std::byte> m_data;
};

class MemoryScanner {
   public:
    [[nodiscard]]
    static std::vector<ScanResult> scanProcess(pid_t pid, MemoryScanLevel level,
                                               const Value& target);

    [[nodiscard]]
    static std::vector<ScanResult> scanRegion(pid_t pid,
                                              const MemoryRegion& region,
                                              const Value& target);

    [[nodiscard]]
    static std::vector<ScanResult> scanBuffer(const MemoryRead& memory,
                                              const Value& target);
};

}  // namespace memseek
