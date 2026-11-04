#pragma once

#include <sys/types.h>

#include <string>

#include "memseek/memory/chunk.hpp"
#include "memseek/memory/read.hpp"

namespace memseek {

class MemoryReader {
   public:
    explicit MemoryReader(pid_t pid) noexcept : m_pid(pid) {}

    [[nodiscard]]
    Result<MemoryRead> read(const MemoryChunk& chunk,
                            const MemoryRegion& region,
                            std::size_t overlap) const;

   private:
    pid_t m_pid{};
};

}  // namespace memseek
