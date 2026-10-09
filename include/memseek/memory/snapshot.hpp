#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "memseek/memory/scan.hpp"

namespace memseek {

// A snapshot is a read-only baseline of candidate memory values.  It does not
// represent a live view of the target process and it does not keep the process
// memory mapping updated automatically.
class MemorySnapshot {
   public:
    using Results = std::vector<ScanResult>;

    // Build one baseline from the complete result set of a scan. Taking the
    // vector by value lets callers move a large result set into the snapshot;
    // the stored representation is still independent from ScanResult.
    [[nodiscard]]
    static auto fromScanResults(pid_t pid, Results results)
        -> MemorySnapshot;

    [[nodiscard]]
    pid_t pid() const noexcept {
        return m_pid;
    }

    [[nodiscard]]
    const Results& results() const noexcept {
        return m_results;
    }

    [[nodiscard]]
    std::size_t size() const noexcept {
        return m_results.size();
    }

    [[nodiscard]]
    bool empty() const noexcept {
        return m_results.empty();
    }

    MemorySnapshot() = delete;

   private:
    MemorySnapshot(pid_t pid, Results results) noexcept
        : m_pid(pid), m_results(std::move(results)) {}

    pid_t m_pid{};
    Results m_results;
};

}  // namespace memseek
