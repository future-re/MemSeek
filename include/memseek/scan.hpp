#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "memseek/memory_read.hpp"
#include "memseek/value.hpp"
namespace memseek {

class ScanResult {
   public:
    explicit ScanResult(uintptr_t address = 0,
                        std::span<const std::byte> data = {})
        : m_address(address), m_data(data.begin(), data.end()) {}

    [[nodiscard]] uintptr_t address() const { return m_address; }

    [[nodiscard]] std::span<const std::byte> data() const { return m_data; }

    [[nodiscard]] bool operator==(const ScanResult& other) const {
        return m_address == other.m_address &&
               m_data.size() == other.m_data.size() &&
               std::equal(m_data.begin(), m_data.end(), other.m_data.begin());
    }

   private:
    uintptr_t m_address{};
    // A scan result must outlive the MemoryRead used by the scanner.  Owning
    // the matched bytes also makes results from process scans safe to return.
    std::vector<std::byte> m_data;
};

class MemoryScanner {
   public:
    [[nodiscard]]
    static std::vector<ScanResult> scanExact(const MemoryRead& memory,
                                             const Value& target);

    [[nodiscard]]
    static std::vector<ScanResult> scanExact(pid_t pid, MemoryScanLevel level,
                                             const Value& target);

    // Scan one already-selected process region.  This is useful for callers
    // that need to measure a known region without scanning the whole process.
    [[nodiscard]]
    static std::vector<ScanResult> scanExact(pid_t pid,
                                             const MemoryRegion& region,
                                             const Value& target);
};

}  // namespace memseek
