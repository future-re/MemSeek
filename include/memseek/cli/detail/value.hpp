#pragma once

#include <string>

#include "memseek/error.hpp"
#include "memseek/value.hpp"

namespace memseek::cli::detail {

[[nodiscard]]
auto parseValue(const std::string& type, const std::string& text)
    -> Result<Value>;

[[nodiscard]]
auto orderedBounds(const Value& lower, const Value& upper) -> bool;

}  // namespace memseek::cli::detail
