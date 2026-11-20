#pragma once

#include <sys/types.h>

#include <cstddef>
#include <memory>
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

    // Move a result set into a baseline, or copy it by passing an lvalue.
    // Copies of the immutable snapshot share the complete result set.
    MemorySnapshot(pid_t pid, Results results)
        : m_pid(pid),
          m_results(std::make_shared<const Results>(std::move(results))) {}

    [[nodiscard]]
    pid_t pid() const noexcept {
        return m_pid;
    }

    [[nodiscard]]
    const Results& results() const noexcept {
        static const Results EMPTY;
        return m_results ? *m_results : EMPTY;
    }

    [[nodiscard]]
    std::size_t size() const noexcept {
        return results().size();
    }

    [[nodiscard]]
    bool empty() const noexcept {
        return results().empty();
    }

   private:
    pid_t m_pid{};
    std::shared_ptr<const Results> m_results;
};

}  // namespace memseek
