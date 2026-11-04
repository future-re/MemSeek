#pragma once

#include <sys/types.h>

#include <cstddef>
#include <optional>
#include <thread>

#include "memseek/error.hpp"
#include "memseek/memory/change.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/memory/snapshot.hpp"
#include "memseek/value.hpp"

namespace memseek {

// Owns the baseline across scans. An empty snapshot is still a valid baseline.
class ScanSession {
   public:
    explicit ScanSession(pid_t pid, std::size_t workerCount =
                                        std::thread::hardware_concurrency());

    // Start a new search and replace the baseline on success.
    [[nodiscard]]
    auto scan(const Value& target, MemoryScanLevel level,
              ScanType type = ScanType::EXACT,
              ByteOrder byteOrder = ByteOrder::NATIVE) -> Result<void>;

    [[nodiscard]]
    auto scan(const Value& lowerBound, const Value& upperBound,
              MemoryScanLevel level, ByteOrder byteOrder = ByteOrder::NATIVE)
        -> Result<void>;

    // Filter the existing baseline and advance it only on success.
    [[nodiscard]]
    auto filter(ChangeType type) -> Result<void>;

    void setPid(pid_t pid) noexcept;
    void reset() noexcept;

    [[nodiscard]]
    auto pid() const noexcept -> pid_t {
        return m_pid;
    }

    // nullptr means no scan has been performed (or reset was called).
    [[nodiscard]]
    auto snapshot() const noexcept -> const MemorySnapshot* {
        return m_snapshot ? &*m_snapshot : nullptr;
    }

   private:
    pid_t m_pid;
    MemoryScanner m_scanner;
    std::optional<MemorySnapshot> m_snapshot;
};

}  // namespace memseek
