#pragma once

namespace version {

inline constexpr int MAJOR_V = 0;
inline constexpr int MINOR_V = 1;
inline constexpr int PATCH_V = 0;

// Keep the existing CLI interface without a mutable shared formatting buffer.
[[nodiscard]]
constexpr auto string() noexcept -> const char* {
    return "0.1.0";
}

}  // namespace version
