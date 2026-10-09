#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "memseek/memory/snapshot.hpp"

namespace memseek {

enum class ChangeType : std::uint8_t {
    CHANGED,
    UNCHANGED,
};

// Empty or incomplete values cannot participate in a comparison.
[[nodiscard]]
auto matchesChange(std::span<const std::byte> previous,
                   std::span<const std::byte> current, ChangeType type) -> bool;

// Read each candidate again and retain matching current values as the next
// baseline. Individual unreadable candidates are discarded. Failure to read
// the process mappings is reported as an error. The input remains unchanged.
[[nodiscard]]
auto captureChanges(const MemorySnapshot& previous, ChangeType type)
    -> std::expected<MemorySnapshot, std::string>;

}  // namespace memseek
