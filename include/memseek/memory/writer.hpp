#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include "memseek/error.hpp"

namespace memseek {

struct MemoryWriteTarget {
    std::uintptr_t address{};
    std::size_t size{};
    // Zero skips the identity check; nonzero must match the current mapping.
    std::uint64_t parentRegionId{};
};

class MemoryWriter {
   public:
    explicit MemoryWriter(pid_t pid) noexcept;

    [[nodiscard]]
    auto write(const MemoryWriteTarget& target, std::span<const std::byte> data)
        -> Result<std::size_t>;

   private:
    pid_t m_pid{};
};

}  // namespace memseek
