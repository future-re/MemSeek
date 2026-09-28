#pragma once

#include <sys/types.h>

#include <expected>
#include <string>

#include "memseek/memory/chunk.hpp"
#include "memseek/memory/read.hpp"

namespace memseek {

class MemoryReader {
   public:
    explicit MemoryReader(pid_t pid) noexcept : m_pid(pid) {}

    [[nodiscard]]
    std::expected<MemoryRead, std::string> read(const MemoryChunk& chunk,
                                                const MemoryRegion& region,
                                                std::size_t overlap) const;

   private:
    pid_t m_pid{};
};

}  // namespace memseek
