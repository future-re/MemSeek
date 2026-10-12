#pragma once

#include <expected>
#include <string>

#include "memseek/value.hpp"

namespace memseek::cli::detail {

[[nodiscard]]
auto parseValue(const std::string& type,
                const std::string& text) -> std::expected<Value, std::string>;

[[nodiscard]]
auto orderedBounds(const Value& lower, const Value& upper) -> bool;

}  // namespace memseek::cli::detail
