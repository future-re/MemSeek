#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace memseek {

// Numeric values are stable for logs and future machine-facing interfaces.
enum class ErrorCode : std::uint8_t {
    INVALID_ARGUMENT =
        1,  // Invalid argument (e.g., null pointer, empty value, etc.)
    INVALID_VALUE =
        2,  // Invalid value (e.g., out-of-range, unsupported type, etc.)
    UNSUPPORTED_OPERATION =
        3,  // Unsupported operation (e.g., write to read-only memory, etc.)
    UNSUPPORTED_PLATFORM = 4,  // Unsupported platform (e.g., non-Linux, etc.)
    MAPPING_QUERY_FAILED =
        5,  // Failed to query process memory mappings (e.g., /proc read error)
    PROCESS_ACCESS_FAILED =
        6,  // Failed to access process memory (e.g., ptrace attach failed)
    ADDRESS_OUT_OF_RANGE =
        7,  // Address range is outside of process memory mappings (e.g.,
            // overflow, unmapped region, chunk, etc.)
    MEMORY_READ_FAILED =
        8,  // Failed to read process memory (e.g., process_vm_readv failed)
    MEMORY_WRITE_FAILED =
        9,  // Failed to write process memory (e.g., process_vm_writev failed)
    MAPPING_NOT_WRITABLE = 10,  // Memory mapping is not writable
    REGION_MISMATCH =
        11,  // Target region ID does not match the writable mapping
    SHORT_TRANSFER = 12,  // Transfer was shorter than expected
    NO_BASELINE = 13,     // shapshot baseline is not available for comparison
};

struct MemSeekError {
    MemSeekError() = default;
    MemSeekError(std::string text) : message(std::move(text)) {}
    MemSeekError(const char* text) : message(text) {}

    ErrorCode code{ErrorCode::INVALID_ARGUMENT};
    std::string message;
    std::error_code systemError;
    std::optional<pid_t> pid;
    std::optional<std::uintptr_t> address;
    std::optional<std::size_t> size;
    std::optional<std::size_t> bytesTransferred;

    bool operator==(const MemSeekError&) const = default;
};

template <typename T>
using Result = std::expected<T, MemSeekError>;

[[nodiscard]]
auto makeError(ErrorCode code, std::string message) -> MemSeekError;

[[nodiscard]]
auto errorCodeName(ErrorCode code) noexcept -> const char*;

[[nodiscard]]
auto formatError(const MemSeekError& error) -> std::string;

}  // namespace memseek
