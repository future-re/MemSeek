#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "memseek/value.hpp"

namespace memseek::test {

template <ValueSupportedType T>
auto encodeValue(T&& value) -> std::vector<std::byte> {
    const Value wrapped(std::forward<T>(value));
    return {wrapped.data().begin(), wrapped.data().end()};
}

}  // namespace memseek::test
