#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include "memseek/error.hpp"

namespace memseek {

class MemoryWriter {
   public:
    explicit MemoryWriter(pid_t pid) noexcept;

    [[nodiscard]]
    auto write(std::uintptr_t address, std::span<const std::byte> data)
        -> Result<std::size_t>;

   private:
    pid_t m_pid{};
};

}  // namespace memseek
