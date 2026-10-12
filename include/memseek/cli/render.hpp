#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "memseek/memory/region.hpp"

namespace memseek::cli {

struct ResultEntry {
    std::uintptr_t address{};
    std::vector<std::byte> currentBytes;
    std::optional<std::vector<std::byte>> previousBytes;
    std::optional<MemoryRegion> region;
};

[[nodiscard]]
auto renderResultsText(std::size_t candidateCount,
                       std::span<const ResultEntry> entries) -> std::string;

[[nodiscard]]
auto renderResultsJson(pid_t pid, std::size_t candidateCount,
                       std::span<const ResultEntry> entries) -> std::string;

[[nodiscard]]
auto renderRegionsText(std::span<const MemoryRegion> regions) -> std::string;

[[nodiscard]]
auto renderHelpText() -> std::string_view;

}  // namespace memseek::cli
