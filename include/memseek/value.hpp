#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace memseek {

template <typename T>
concept ValueNumericType =
    std::same_as<std::remove_cvref_t<T>, std::uint8_t> ||
    std::same_as<std::remove_cvref_t<T>, std::uint16_t> ||
    std::same_as<std::remove_cvref_t<T>, std::uint32_t> ||
    std::same_as<std::remove_cvref_t<T>, std::uint64_t> ||
    std::same_as<std::remove_cvref_t<T>, std::int8_t> ||
    std::same_as<std::remove_cvref_t<T>, std::int16_t> ||
    std::same_as<std::remove_cvref_t<T>, std::int32_t> ||
    std::same_as<std::remove_cvref_t<T>, std::int64_t> ||
    std::same_as<std::remove_cvref_t<T>, float> ||
    std::same_as<std::remove_cvref_t<T>, double>;

template <typename T>
concept ValueSupportedType =
    ValueNumericType<T> || std::same_as<std::remove_cvref_t<T>, std::string> ||
    std::same_as<std::remove_cvref_t<T>, std::vector<std::byte>>;

enum class ValueType : uint8_t {
    UNKNOWN = 0x00,    // unknown type
    U_INT8 = 0x10,     // uint8_t
    U_INT16 = 0x11,    // uint16_t
    U_INT32 = 0x12,    // uint32_t
    U_INT64 = 0x13,    // uint64_t
    INT8 = 0x14,       // int8_t
    INT16 = 0x15,      // int16_t
    INT32 = 0x16,      // int32_t
    INT64 = 0x17,      // int64_t
    FLOAT32 = 0x20,    // float
    FLOAT64 = 0x21,    // double
    STR = 0x30,        // string
    BIN_ARRAY = 0x31,  // binary data
};

template <ValueSupportedType T>
constexpr ValueType getValueType() {
    using CleanT = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<CleanT, uint8_t>) {
        return ValueType::U_INT8;
    } else if constexpr (std::is_same_v<CleanT, uint16_t>) {
        return ValueType::U_INT16;
    } else if constexpr (std::is_same_v<CleanT, uint32_t>) {
        return ValueType::U_INT32;
    } else if constexpr (std::is_same_v<CleanT, uint64_t>) {
        return ValueType::U_INT64;
    } else if constexpr (std::is_same_v<CleanT, int8_t>) {
        return ValueType::INT8;
    } else if constexpr (std::is_same_v<CleanT, int16_t>) {
        return ValueType::INT16;
    } else if constexpr (std::is_same_v<CleanT, int32_t>) {
        return ValueType::INT32;
    } else if constexpr (std::is_same_v<CleanT, int64_t>) {
        return ValueType::INT64;
    } else if constexpr (std::is_same_v<CleanT, float>) {
        return ValueType::FLOAT32;
    } else if constexpr (std::is_same_v<CleanT, double>) {
        return ValueType::FLOAT64;
    } else if constexpr (std::is_same_v<CleanT, std::string>) {
        return ValueType::STR;
    } else if constexpr (std::is_same_v<CleanT, std::vector<std::byte>>) {
        return ValueType::BIN_ARRAY;
    } else {
        static_assert(false, "Unsupported type");
    }
}

class Value {
   private:
    ValueType m_type{ValueType::UNKNOWN};
    std::vector<std::byte> m_data;

   public:
    template <ValueSupportedType T>
    explicit Value(T&& value) : m_type(getValueType<T>()) {
        using CleanT = std::remove_cvref_t<T>;

        if constexpr (std::same_as<CleanT, std::string>) {
            const auto* begin =
                reinterpret_cast<const std::byte*>(value.data());

            m_data.assign(begin, begin + value.size());

        } else if constexpr (std::same_as<CleanT, std::vector<std::byte>>) {
            m_data = std::forward<T>(value);

        } else if constexpr (ValueNumericType<CleanT>) {
            m_data.resize(sizeof(CleanT));
            std::memcpy(m_data.data(), std::addressof(value), sizeof(CleanT));
        }
    }

    template <ValueSupportedType T>
    std::remove_cvref_t<T> as() const {
        using CleanT = std::remove_cvref_t<T>;

        if (m_type != getValueType<CleanT>()) {
            throw std::runtime_error("Value type mismatch");
        }

        if constexpr (std::same_as<CleanT, std::string>) {
            return std::string{reinterpret_cast<const char*>(m_data.data()),
                               m_data.size()};

        } else if constexpr (std::same_as<CleanT, std::vector<std::byte>>) {
            return m_data;

        } else {
            CleanT value{};

            std::memcpy(&value, m_data.data(), sizeof(CleanT));

            return value;
        }
    }

    [[nodiscard]]
    ValueType type() const noexcept {
        return m_type;
    }

    [[nodiscard]]
    std::span<const std::byte> data() const noexcept {
        return m_data;
    }

    [[nodiscard]]
    bool operator==(std::span<const std::byte> other) const noexcept {
        return m_data.size() == other.size() &&
               std::memcmp(m_data.data(), other.data(), m_data.size()) == 0;
    }

    [[nodiscard]]
    std::string_view asStringRef() const {
        if (m_type != ValueType::STR) {
            throw std::runtime_error("Value is not a string");
        }

        return {reinterpret_cast<const char*>(m_data.data()), m_data.size()};
    }

    [[nodiscard]]
    std::size_t size() const noexcept {
        return m_data.size();
    }

    [[nodiscard]]
    bool empty() const noexcept {
        return m_data.empty();
    }
};
}  // namespace memseek