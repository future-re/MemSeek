#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "memseek/error.hpp"

namespace memseek {

class MemoryReader {
   public:
    explicit MemoryReader(pid_t pid) noexcept : m_pid(pid) {}

    // Reads exactly size bytes, or returns an error without partial data.
    // An empty request succeeds without accessing the process.
    [[nodiscard]]
    auto read(std::uintptr_t address, std::size_t size) const
        -> Result<std::vector<std::byte>>;

   private:
    pid_t m_pid{};
};

}  // namespace memseek
