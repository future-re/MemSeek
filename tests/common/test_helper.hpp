#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

template <typename T>
concept ValueSupportedType =
    std::same_as<std::remove_cvref_t<T>, std::string> ||
    std::same_as<std::remove_cvref_t<T>, std::vector<std::byte>> ||
    std::same_as<std::remove_cvref_t<T>, float> ||
    std::same_as<std::remove_cvref_t<T>, double> ||
    std::same_as<std::remove_cvref_t<T>, uint8_t> ||
    std::same_as<std::remove_cvref_t<T>, uint16_t> ||
    std::same_as<std::remove_cvref_t<T>, uint32_t> ||
    std::same_as<std::remove_cvref_t<T>, uint64_t> ||
    std::same_as<std::remove_cvref_t<T>, int8_t> ||
    std::same_as<std::remove_cvref_t<T>, int16_t> ||
    std::same_as<std::remove_cvref_t<T>, int32_t> ||
    std::same_as<std::remove_cvref_t<T>, int64_t> ||
    std::same_as<std::remove_cvref_t<T>, float> ||
    std::same_as<std::remove_cvref_t<T>, double>;
template <ValueSupportedType T>
std::vector<std::byte> changeValue(T value) {
    using CleanT = std::remove_cvref_t<T>;
    if constexpr (std::same_as<CleanT, std::string>) {
        const auto* begin = reinterpret_cast<const std::byte*>(value.data());
        return std::vector<std::byte>(begin, begin + value.size());
    } else if constexpr (std::same_as<CleanT, std::vector<std::byte>>) {
        return value;
    } else if constexpr (std::is_arithmetic_v<CleanT>) {
        std::vector<std::byte> data(sizeof(CleanT));
        std::memcpy(data.data(), std::addressof(value), sizeof(CleanT));
        return data;
    } else {
        static_assert(false, "Unsupported type");
    }
}

inline std::vector<std::byte> operator+(const std::vector<std::byte>& left,
                                        const std::vector<std::byte>& right) {
    std::vector<std::byte> result;
    result.reserve(left.size() + right.size());

    result.insert(result.end(), left.begin(), left.end());
    result.insert(result.end(), right.begin(), right.end());

    return result;
}

inline std::vector<std::byte>& operator+=(std::vector<std::byte>& left,
                                          const std::vector<std::byte>& right) {
    left.insert(left.end(), right.begin(), right.end());
    return left;
}