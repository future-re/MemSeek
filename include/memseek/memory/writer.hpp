#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace memseek {

struct MemoryWriteTarget {
    std::uintptr_t address{};
    std::size_t size{};
    std::uint64_t parentRegionId{};
};

class MemoryWriter {
   public:
    explicit MemoryWriter(pid_t pid) noexcept;

    [[nodiscard]]
    auto write(const MemoryWriteTarget& target, std::span<const std::byte> data)
        -> std::expected<std::size_t, std::string>;

   private:
    pid_t m_pid{};
};

}  // namespace memseek