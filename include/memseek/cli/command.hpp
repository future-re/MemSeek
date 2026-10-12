#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "memseek/memory/change.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/value.hpp"

namespace memseek::cli {

enum class OutputFormat : std::uint8_t {
    TEXT,
    JSON,
};

struct AttachCommand {
    pid_t pid{};
};

struct ScanCommand {
    ScanType scanType{ScanType::EXACT};
    Value lowerBound;
    std::optional<Value> upperBound;
    MemoryScanLevel level;
    std::string description;
};

struct FilterCommand {
    ChangeType type{ChangeType::CHANGED};
};

struct ResultsCommand {
    std::optional<std::size_t> limit;
    OutputFormat format{OutputFormat::TEXT};
};

struct RegionsCommand {
    MemoryScanLevel level;
};

struct InfoCommand {};
struct ResetCommand {};
struct HelpCommand {};
struct QuitCommand {};

using Command = std::variant<AttachCommand, ScanCommand, FilterCommand,
                             ResultsCommand, RegionsCommand, InfoCommand,
                             ResetCommand, HelpCommand, QuitCommand>;

[[nodiscard]]
auto parseCommandLine(std::string_view line)
    -> std::expected<std::optional<Command>, std::string>;

[[nodiscard]]
auto parseByteOrder(std::string_view text) -> std::optional<ByteOrder>;

[[nodiscard]]
auto byteOrderText(ByteOrder byteOrder) -> std::string_view;

}  // namespace memseek::cli
