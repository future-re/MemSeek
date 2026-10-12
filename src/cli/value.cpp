#include "memseek/cli/detail/value.hpp"

#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "memseek/value.hpp"

namespace memseek::cli::detail {

namespace {

template <typename T>
auto parseInteger(const std::string_view text) -> std::optional<T> {
    T value{};
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

template <typename T>
auto parseFloating(const std::string& text) -> std::optional<T> {
    errno = 0;
    char* end = nullptr;
    T value{};
    if constexpr (std::is_same_v<T, float>) {
        value = std::strtof(text.c_str(), &end);
    } else {
        value = std::strtod(text.c_str(), &end);
    }
    if (end == text.c_str() || *end != '\0' || errno == ERANGE ||
        !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

auto parseBytes(const std::string_view text)
    -> std::optional<std::vector<std::byte>> {
    std::string compact;
    for (const char character : text) {
        if (std::isxdigit(static_cast<unsigned char>(character)) != 0) {
            compact += character;
        } else if (std::isspace(static_cast<unsigned char>(character)) == 0 &&
                   character != ':' && character != ',') {
            return std::nullopt;
        }
    }
    if (compact.empty() || compact.size() % 2 != 0) {
        return std::nullopt;
    }

    std::vector<std::byte> bytes;
    bytes.reserve(compact.size() / 2);
    for (std::size_t index = 0; index < compact.size(); index += 2) {
        unsigned int value{};
        const auto result = std::from_chars(
            compact.data() + index, compact.data() + index + 2, value, 16);
        if (result.ec != std::errc{} ||
            result.ptr != compact.data() + index + 2) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

template <typename T>
auto compareBounds(const Value& lower, const Value& upper) -> bool {
    return lower.as<T>() <= upper.as<T>();
}

}  // namespace

auto parseValue(const std::string& type,
                const std::string& text) -> std::expected<Value, std::string> {
    if (type == "u8") {
        if (const auto value = parseInteger<std::uint8_t>(text)) {
            return Value{*value};
        }
    } else if (type == "u16") {
        if (const auto value = parseInteger<std::uint16_t>(text)) {
            return Value{*value};
        }
    } else if (type == "u32") {
        if (const auto value = parseInteger<std::uint32_t>(text)) {
            return Value{*value};
        }
    } else if (type == "u64") {
        if (const auto value = parseInteger<std::uint64_t>(text)) {
            return Value{*value};
        }
    } else if (type == "i8") {
        if (const auto value = parseInteger<std::int8_t>(text)) {
            return Value{*value};
        }
    } else if (type == "i16") {
        if (const auto value = parseInteger<std::int16_t>(text)) {
            return Value{*value};
        }
    } else if (type == "i32") {
        if (const auto value = parseInteger<std::int32_t>(text)) {
            return Value{*value};
        }
    } else if (type == "i64") {
        if (const auto value = parseInteger<std::int64_t>(text)) {
            return Value{*value};
        }
    } else if (type == "float") {
        if (const auto value = parseFloating<float>(text)) {
            return Value{*value};
        }
    } else if (type == "double") {
        if (const auto value = parseFloating<double>(text)) {
            return Value{*value};
        }
    } else if (type == "string") {
        if (!text.empty()) {
            return Value{std::string{text}};
        }
    } else if (type == "bytes") {
        if (const auto value = parseBytes(text)) {
            return Value{*value};
        }
    } else {
        return std::unexpected("unsupported value type: " + type);
    }
    return std::unexpected("invalid " + type + " value: " + text);
}

auto orderedBounds(const Value& lower, const Value& upper) -> bool {
    switch (lower.type()) {
        case ValueType::U_INT8:
            return compareBounds<std::uint8_t>(lower, upper);
        case ValueType::U_INT16:
            return compareBounds<std::uint16_t>(lower, upper);
        case ValueType::U_INT32:
            return compareBounds<std::uint32_t>(lower, upper);
        case ValueType::U_INT64:
            return compareBounds<std::uint64_t>(lower, upper);
        case ValueType::INT8:
            return compareBounds<std::int8_t>(lower, upper);
        case ValueType::INT16:
            return compareBounds<std::int16_t>(lower, upper);
        case ValueType::INT32:
            return compareBounds<std::int32_t>(lower, upper);
        case ValueType::INT64:
            return compareBounds<std::int64_t>(lower, upper);
        case ValueType::FLOAT32:
            return compareBounds<float>(lower, upper);
        case ValueType::FLOAT64:
            return compareBounds<double>(lower, upper);
        case ValueType::UNKNOWN:
        case ValueType::STR:
        case ValueType::BIN_ARRAY:
            return false;
    }
    return false;
}

}  // namespace memseek::cli::detail
